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
   virtual VkResult get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surface_format_count,
                                        VkSurfaceFormatKHR *surface_formats) = 0;

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

protected:
   /**
    * @brief Helper function for the vkGetPhysicalDeviceSurfaceFormatsKHR entrypoint.
    *
    * Implements the common logic, which is used by all the WSI backends for
    * setting the supported formats by the surface.
    *
    * @param begin                 Beginning of an iterator with the supported VkFormats.
    * @param end                   End of the iterator.
    * @param surface_formats_count Pointer for setting the length of the supported
    *                              formats.
    * @param surface_formats       The supported formats by the surface.
    *
    * return VK_SUCCESS on success, an appropriate error code otherwise.
    *
    */
   template <typename It>
   VkResult set_surface_formats(It begin, It end, uint32_t *surface_formats_count, VkSurfaceFormatKHR *surface_formats)
   {
      assert(surface_formats_count != nullptr);

      const uint32_t supported_formats_count = std::distance(begin, end);
      if (surface_formats == nullptr)
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
      std::for_each(begin, end, [&](const VkFormat &format) {
         if (format_count < *surface_formats_count)
         {
            surface_formats[format_count].format = format;
            surface_formats[format_count++].colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
         }
      });

      return res;
   }
};

} /* namespace wsi */
