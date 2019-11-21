/*
 * Copyright (c) 2016-2017, 2019 Arm Limited.
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

#include <cassert>

#include <wsi/wsi_factory.hpp>

#include "private_data.hpp"
#include "surface_api.hpp"

extern "C"
{

   /**
    * @brief Implements vkGetPhysicalDeviceSurfaceCapabilitiesKHR Vulkan entrypoint.
    */
   VKAPI_ATTR VkResult wsi_layer_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
   {
      wsi::surface_properties *props = wsi::get_surface_properties(surface);
      if (props)
      {
         return props->get_surface_capabilities(physicalDevice, surface, pSurfaceCapabilities);
      }

      return layer::instance_private_data::get(layer::get_key(physicalDevice))
         .disp.GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
   }

   /**
    * @brief Implements vkGetPhysicalDeviceSurfaceFormatsKHR Vulkan entrypoint.
    */
   VKAPI_ATTR VkResult wsi_layer_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                                                      VkSurfaceKHR surface,
                                                                      uint32_t *pSurfaceFormatCount,
                                                                      VkSurfaceFormatKHR *pSurfaceFormats)
   {
      wsi::surface_properties *props = wsi::get_surface_properties(surface);
      if (props)
      {
         return props->get_surface_formats(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
      }

      return layer::instance_private_data::get(layer::get_key(physicalDevice))
         .disp.GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
   }

   /**
    * @brief Implements vkGetPhysicalDeviceSurfacePresentModesKHR Vulkan entrypoint.
    */
   VKAPI_ATTR VkResult wsi_layer_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                                                           VkSurfaceKHR surface,
                                                                           uint32_t *pPresentModeCount,
                                                                           VkPresentModeKHR *pPresentModes)
   {
      wsi::surface_properties *props = wsi::get_surface_properties(surface);
      if (props)
      {
         return props->get_surface_present_modes(physicalDevice, surface, pPresentModeCount, pPresentModes);
      }

      return layer::instance_private_data::get(layer::get_key(physicalDevice))
         .disp.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
   }

   /**
    * @brief Implements vkGetPhysicalDeviceSurfaceSupportKHR Vulkan entrypoint.
    */
   VKAPI_ATTR VkResult wsi_layer_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                                                      uint32_t queueFamilyIndex, VkSurfaceKHR surface,
                                                                      VkBool32 *pSupported)
   {
      wsi::surface_properties *props = wsi::get_surface_properties(surface);
      /* We assume that presentation to surface is supported by default */
      if (props)
      {
         *pSupported = VK_TRUE;
         return VK_SUCCESS;
      }

      return layer::instance_private_data::get(layer::get_key(physicalDevice))
         .disp.GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
   }

} /* extern "C" */
