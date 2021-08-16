/*
 * Copyright (c) 2017-2019, 2021 Arm Limited.
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
   virtual VkResult get_surface_capabilities(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                             VkSurfaceCapabilitiesKHR *surface_capabilities) = 0;

   /**
    * @brief Implementation of vkGetPhysicalDeviceSurfaceFormatsKHR for the specific VkSurface type.
    */
   virtual VkResult get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                        uint32_t *surface_format_count, VkSurfaceFormatKHR *surface_formats) = 0;

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

   /* There is no maximum theoretically speaking however we choose 3 for practicality */
   static constexpr uint32_t MAX_SWAPCHAIN_IMAGE_COUNT = 3;
};

} /* namespace wsi */
