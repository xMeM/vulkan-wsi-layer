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

#define VK_USE_PLATFORM_ANDROID_KHR 1

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>

#include <ostream>
#include <vector>
#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_core.h>

#include <layer/private_data.hpp>

#include "surface_properties.hpp"
#include "surface.hpp"
#include "util/macros.hpp"
#include "wsi/headless/surface.hpp"
#include "wsi/surface_properties.hpp"

#define UNUSED(x) ((void)(x))

namespace wsi
{
namespace x11
{

surface_properties &surface_properties::get_instance()
{
   static surface_properties _instance;
   return _instance;
}

surface_properties::surface_properties(surface &wsi_surface, const util::allocator &allocator)
   : specific_surface(&wsi_surface)
{
}

surface_properties::surface_properties()
   : specific_surface(nullptr)
{
}

VkResult surface_properties::get_surface_capabilities(VkPhysicalDevice physical_device,
                                                      VkSurfaceCapabilitiesKHR *surface_capabilities)
{
   get_surface_capabilities_common(physical_device, surface_capabilities);
   VkExtent2D extent;
   int depth;
   specific_surface->getWindowSizeAndDepth(&extent, &depth);
   surface_capabilities->currentExtent = extent;
   surface_capabilities->minImageExtent = extent;
   surface_capabilities->maxImageExtent = extent;

   surface_capabilities->minImageCount = 4;
   surface_capabilities->maxImageCount = 0;

   surface_capabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   surface_capabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   surface_capabilities->maxImageArrayLayers = 1;
   surface_capabilities->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

std::vector<VkFormat> support_formats{ VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM };

VkResult surface_properties::get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surface_format_count,
                                                 VkSurfaceFormatKHR *surface_formats,
                                                 VkSurfaceFormat2KHR *extended_surface_formats)
{
   std::vector<surface_format_properties> formats;
   for (auto &format : support_formats)
   {
      if (format != VK_FORMAT_UNDEFINED)
      {
         formats.insert(formats.begin(), (surface_format_properties){ format });
      }
   }
   return surface_properties_formats_helper(formats.begin(), formats.end(), surface_format_count, surface_formats,
                                            extended_surface_formats);
}

VkResult surface_properties::get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                       uint32_t *present_mode_count, VkPresentModeKHR *present_modes)
{
   UNUSED(physical_device);
   UNUSED(surface);

   static const std::array<VkPresentModeKHR, 2> modes = {
      VK_PRESENT_MODE_MAILBOX_KHR,
      VK_PRESENT_MODE_FIFO_KHR,
   };

   return get_surface_present_modes_common(present_mode_count, present_modes, modes);
}

static const char *required_device_extensions[] = {
   VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
   VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
   VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
   VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
   VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
   VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
   VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
   VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
   VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
   VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
   VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
   VK_KHR_MAINTENANCE1_EXTENSION_NAME,
   VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
};

VkResult surface_properties::get_required_device_extensions(util::extension_list &extension_list)
{
   return extension_list.add(required_device_extensions,
                             sizeof(required_device_extensions) / sizeof(required_device_extensions[0]));
}

VWL_VKAPI_CALL(VkResult)
CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) VWL_API_POST
{

   auto &instance_data = layer::instance_private_data::get(instance);
   util::allocator allocator{ instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pAllocator };

   auto wsi_surface = surface::make_surface(allocator, pCreateInfo->connection, pCreateInfo->window);
   if (wsi_surface == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   auto surface_base = util::unique_ptr<wsi::surface>(std::move(wsi_surface));
   VkResult res = instance_data.disp.CreateXcbSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
   if (res == VK_SUCCESS)
   {
      res = instance_data.add_surface(*pSurface, surface_base);
      if (res != VK_SUCCESS)
      {
         instance_data.disp.DestroySurfaceKHR(instance, *pSurface, pAllocator);
      }
   }
   return res;
}

static bool visual_supported(xcb_visualtype_t *visual)
{
   if (!visual)
      return false;

   return visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR || visual->_class == XCB_VISUAL_CLASS_DIRECT_COLOR;
}

static xcb_visualtype_t *screen_get_visualtype(xcb_screen_t *screen, xcb_visualid_t visual_id, unsigned *depth)
{
   xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);

   for (; depth_iter.rem; xcb_depth_next(&depth_iter))
   {
      xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);

      for (; visual_iter.rem; xcb_visualtype_next(&visual_iter))
      {
         if (visual_iter.data->visual_id == visual_id)
         {
            if (depth)
               *depth = depth_iter.data->depth;
            return visual_iter.data;
         }
      }
   }

   return NULL;
}

static xcb_visualtype_t *connection_get_visualtype(xcb_connection_t *conn, xcb_visualid_t visual_id)
{
   xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(xcb_get_setup(conn));

   /* For this we have to iterate over all of the screens which is rather
    * annoying.  Fortunately, there is probably only 1.
    */
   for (; screen_iter.rem; xcb_screen_next(&screen_iter))
   {
      xcb_visualtype_t *visual = screen_get_visualtype(screen_iter.data, visual_id, NULL);
      if (visual)
         return visual;
   }

   return NULL;
}

VWL_VKAPI_CALL(VkResult)
GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
                                   VkBool32 *pSupported)
{
   *pSupported = VK_TRUE;
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkBool32)
GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                           xcb_connection_t *connection, xcb_visualid_t visual_id)
{
   bool dev_supports_sync =
      sync_fd_fence_sync::is_supported(layer::instance_private_data::get(physicalDevice), physicalDevice);
   if (!dev_supports_sync)
   {
      return VK_FALSE;
   }

   if (!visual_supported(connection_get_visualtype(connection, visual_id)))
      return false;

   return VK_TRUE;
}

VWL_VKAPI_CALL(VkResult)
CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
   const VkXcbSurfaceCreateInfoKHR CreateInfo = {
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .flags = 0,
      .pNext = NULL,
      .connection = XGetXCBConnection(pCreateInfo->dpy),
      .window = static_cast<xcb_window_t>(pCreateInfo->window),
   };
   return CreateXcbSurfaceKHR(instance, &CreateInfo, pAllocator, pSurface);
}

VWL_VKAPI_CALL(VkBool32)
GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
                                            VisualID visualID)
{
   return GetPhysicalDeviceXcbPresentationSupportKHR(physicalDevice, queueFamilyIndex, XGetXCBConnection(dpy),
                                                     visualID);
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{
   if (strcmp(name, "vkCreateXcbSurfaceKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateXcbSurfaceKHR);
   }
   if (strcmp(name, "vkCreateXlibSurfaceKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateXlibSurfaceKHR);
   }
   if (strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceSupportKHR);
   }
   if (strcmp(name, "vkGetPhysicalDeviceXcbPresentationSupportKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceXcbPresentationSupportKHR);
   }
   if (strcmp(name, "vkGetPhysicalDeviceXlibPresentationSupportKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceXlibPresentationSupportKHR);
   }
   return nullptr;
}

bool surface_properties::is_surface_extension_enabled(const layer::instance_private_data &instance_data)
{
   return instance_data.is_instance_extension_enabled(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
}

} /* namespace x11 */
} /* namespace wsi */
