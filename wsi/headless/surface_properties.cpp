/*
 * Copyright (c) 2017-2019, 2021-2024 Arm Limited.
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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>

#include <layer/private_data.hpp>

#include "surface_properties.hpp"
#include "surface.hpp"
#include "util/macros.hpp"

namespace wsi
{
namespace headless
{

constexpr int max_core_1_0_formats = VK_FORMAT_ASTC_12x12_SRGB_BLOCK + 1;

surface_properties &surface_properties::get_instance()
{
   static surface_properties instance;
   return instance;
}

VkResult surface_properties::get_surface_capabilities(VkPhysicalDevice physical_device,
                                                      VkSurfaceCapabilitiesKHR *surface_capabilities)
{
   get_surface_capabilities_common(physical_device, surface_capabilities);

   return VK_SUCCESS;
}

static uint32_t fill_supported_formats(VkPhysicalDevice physical_device,
                                       std::array<surface_format_properties, max_core_1_0_formats> &formats)
{
   uint32_t format_count = 0;
   for (int id = 0; id < max_core_1_0_formats; id++)
   {
      formats[format_count] = surface_format_properties{ static_cast<VkFormat>(id) };

      VkPhysicalDeviceImageFormatInfo2KHR format_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR,
                                                          nullptr,
                                                          static_cast<VkFormat>(id),
                                                          VK_IMAGE_TYPE_2D,
                                                          VK_IMAGE_TILING_OPTIMAL,
                                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                          0 };

      VkResult res = formats[format_count].check_device_support(physical_device, format_info);

      if (res == VK_SUCCESS)
      {
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
         if (layer::instance_private_data::get(physical_device).has_image_compression_support(physical_device))
         {
            formats[format_count].add_device_compression_support(physical_device, format_info);
         }
#endif
         format_count++;
      }
   }

   return format_count;
}

VkResult surface_properties::get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surface_format_count,
                                                 VkSurfaceFormatKHR *surface_formats,
                                                 VkSurfaceFormat2KHR *extended_surface_formats)
{
   /* Construct a list of all formats supported by the driver - for color attachment */
   std::array<surface_format_properties, max_core_1_0_formats> formats{};
   auto format_count = fill_supported_formats(physical_device, formats);

   return surface_properties_formats_helper(formats.begin(), formats.begin() + format_count, surface_format_count,
                                            surface_formats, extended_surface_formats);
}

VkResult surface_properties::get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                       uint32_t *present_mode_count, VkPresentModeKHR *present_modes)
{
   UNUSED(physical_device);
   UNUSED(surface);

   static const std::array<VkPresentModeKHR, 4> modes = {
      VK_PRESENT_MODE_FIFO_KHR,
      VK_PRESENT_MODE_FIFO_RELAXED_KHR,
      VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,
      VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR,
   };

   return get_surface_present_modes_common(present_mode_count, present_modes, modes);
}

VWL_VKAPI_CALL(VkResult)
CreateHeadlessSurfaceEXT(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) VWL_API_POST
{
   auto &instance_data = layer::instance_private_data::get(instance);
   util::allocator allocator{ instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pAllocator };
   auto wsi_surface = util::unique_ptr<wsi::surface>(allocator.make_unique<surface>());
   if (wsi_surface == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   VkResult res = instance_data.disp.CreateHeadlessSurfaceEXT(instance, pCreateInfo, pAllocator, pSurface);
   if (res == VK_SUCCESS)
   {
      res = instance_data.add_surface(*pSurface, wsi_surface);
      if (res != VK_SUCCESS)
      {
         instance_data.disp.DestroySurfaceKHR(instance, *pSurface, pAllocator);
      }
   }
   return res;
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{
   if (strcmp(name, "vkCreateHeadlessSurfaceEXT") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateHeadlessSurfaceEXT);
   }
   return nullptr;
}

VkResult surface_properties::get_required_instance_extensions(util::extension_list &extension_list)
{
   const std::array required_instance_extensions{
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
   };
   return extension_list.add(required_instance_extensions.data(), required_instance_extensions.size());
}

bool surface_properties::is_surface_extension_enabled(const layer::instance_private_data &instance_data)
{
   return instance_data.is_instance_extension_enabled(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
}

} /* namespace headless */
} /* namespace wsi */
