/*
 * Copyright (c) 2017-2019, 2021-2022 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file surface_properties.hpp
 *
 * @brief Vulkan WSI surface query interfaces.
 */

#pragma once

#include <vulkan/vulkan.h>
#include "util/extension_list.hpp"
#include "layer/private_data.hpp"
#include "util/custom_allocator.hpp"
#include "util/log.hpp"
#include "util/format_modifiers.hpp"
#include "util/drm/drm_utils.hpp"

namespace wsi
{

/**
 * @brief The base surface property query interface.
 */
class surface_properties
{
public:
   /**
    * @brief Implementation of vkGetPhysicalDeviceSurfaceCapabilitiesKHR for the specific VkSurface type.
    */
   virtual VkResult get_surface_capabilities(VkPhysicalDevice physical_device,
                                             VkSurfaceCapabilitiesKHR *surface_capabilities) = 0;

   /**
    * @brief Implementation of vkGetPhysicalDeviceSurfaceFormatsKHR for the specific VkSurface type.
    */
   virtual VkResult get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surface_formats_count,
                                        VkSurfaceFormatKHR *surface_formats,
                                        VkSurfaceFormat2KHR *extended_surface_formats = nullptr) = 0;

   /**
    * @brief Implementation of vkGetPhysicalDeviceSurfacePresentModesKHR for the specific VkSurface type.
    */
   virtual VkResult get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                              uint32_t *present_mode_count, VkPresentModeKHR *present_modes) = 0;

   /**
    * @brief Return the device extensions that this surface_properties implementation needs.
    */
   virtual VkResult get_required_device_extensions(util::extension_list &extension_list)
   {
      /* Requires no additional extensions */
      return VK_SUCCESS;
   }

   /**
    * @brief Implements vkGetProcAddr for entrypoints specific to the surface type.
    *
    * At least the specific VkSurface creation entrypoint must be intercepted.
    */
   virtual PFN_vkVoidFunction get_proc_addr(const char *name) = 0;

   /**
    * @brief Check if the proper surface extension has been enabled for the specific VkSurface type.
    */
   virtual bool is_surface_extension_enabled(const layer::instance_private_data &instance_data) = 0;

   /* There is no maximum theoretically speaking however we choose 3 for practicality */
   static constexpr uint32_t MAX_SWAPCHAIN_IMAGE_COUNT = 3;

};

class surface_format_properties
{
public:
   surface_format_properties(VkFormat format)
      : m_surface_format{ format, VK_COLORSPACE_SRGB_NONLINEAR_KHR }
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
      , m_compression{ VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT, nullptr, VK_IMAGE_COMPRESSION_DEFAULT_EXT,
                       VK_IMAGE_COMPRESSION_FIXED_RATE_NONE_EXT }
#endif
   {
   }

   surface_format_properties()
      : surface_format_properties{ VK_FORMAT_UNDEFINED }
   {
   }

   VkResult check_device_support(VkPhysicalDevice phys_dev, VkPhysicalDeviceImageFormatInfo2 image_format_info);

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   VkResult add_device_compression_support(VkPhysicalDevice phys_dev,
                                           VkPhysicalDeviceImageFormatInfo2 image_format_info);
#endif

   void fill_format_properties(VkSurfaceFormat2KHR &surf_format) const;

   template <typename K>
   static surface_format_properties& from_iterator(std::pair<const K, surface_format_properties> &iter)
   {
      return iter.second;
   }
   static surface_format_properties& from_iterator(surface_format_properties &iter)
   {
      return iter;
   }

   VkSurfaceFormatKHR m_surface_format;

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
private:
   VkImageCompressionPropertiesEXT m_compression;
#endif
};

/**
 * @brief Helper function for the vkGetPhysicalDeviceSurfaceFormatsKHR entrypoint.
 *
 * Implements the common logic, which is used by all the WSI backends for
 * setting the supported formats by the surface.
 *
 * @param begin                    Beginning of an iterator with the supported @ref surface_format_properties
 * @param end                      End of the iterator.
 * @param surface_formats_count    Pointer for setting the length of the supported
 *                                 formats.
 * @param surface_formats          The supported formats by the surface.
 * @param extended_surface_formats Extended surface formats supported by the surface, it
 *                                 is being used when the vkGetPhysicalDeviceSurfaceFormats2KHR
 *                                 entrypoint is used.
 *
 * @return VK_SUCCESS on success, an appropriate error code otherwise.
 *
 */
template <typename It>
VkResult surface_properties_formats_helper(It begin, It end, uint32_t *surface_formats_count,
                                           VkSurfaceFormatKHR *surface_formats,
                                           VkSurfaceFormat2KHR *extended_surface_formats)
{
   assert(surface_formats_count != nullptr);

   const uint32_t supported_formats_count = std::distance(begin, end);
   if (surface_formats == nullptr && extended_surface_formats == nullptr)
   {
      *surface_formats_count = supported_formats_count;
      return VK_SUCCESS;
   }

   VkResult res = VK_SUCCESS;
   if (supported_formats_count > *surface_formats_count)
   {
      res = VK_INCOMPLETE;
   }

   *surface_formats_count = std::min(*surface_formats_count, supported_formats_count);
   uint32_t format_count = 0;
   for (auto it = begin; it != end; ++it)
   {
      const auto &props = surface_format_properties::from_iterator(*it);

      if (format_count >= *surface_formats_count)
      {
         break;
      }

      if (extended_surface_formats == nullptr)
      {
         surface_formats[format_count] = props.m_surface_format;
      }
      else
      {
         props.fill_format_properties(extended_surface_formats[format_count]);
      }

      format_count++;
   }

   return res;
}


} /* namespace wsi */
