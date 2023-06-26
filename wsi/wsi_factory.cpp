/*
 * Copyright (c) 2019-2024 Arm Limited.
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
 * @file
 * @brief Implements factory methods for obtaining the specific surface and swapchain implementations.
 */

#include "wsi_factory.hpp"
#include "surface.hpp"

#if BUILD_WSI_HEADLESS
#include "headless/surface_properties.hpp"
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

#include <vulkan/vk_icd.h>

#if BUILD_WSI_WAYLAND
#include <vulkan/vulkan_wayland.h>
#include "wayland/surface_properties.hpp"
#endif

#if BUILD_WSI_DISPLAY
#include "display/surface_properties.hpp"
#endif

#if BUILD_WSI_X11
#include <X11/Xlib-xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include "x11/surface_properties.hpp"
#endif

namespace wsi
{

static struct wsi_extension
{
   VkExtensionProperties extension;
   VkIcdWsiPlatform platform;
} const supported_wsi_extensions[] = {
#if BUILD_WSI_HEADLESS
   { { VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_HEADLESS },
#endif
#if BUILD_WSI_WAYLAND
   { { VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_WAYLAND },
#endif
#if BUILD_WSI_DISPLAY
   { { VK_KHR_DISPLAY_EXTENSION_NAME, VK_KHR_DISPLAY_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_DISPLAY },
#endif
#if BUILD_WSI_X11
   { { VK_KHR_XCB_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_XCB },
   { { VK_KHR_XLIB_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_XLIB },
#endif
};

static surface_properties *get_surface_properties(VkIcdWsiPlatform platform)
{
   switch (platform)
   {
#if BUILD_WSI_HEADLESS
   case VK_ICD_WSI_PLATFORM_HEADLESS:
      return &headless::surface_properties::get_instance();
#endif
#if BUILD_WSI_WAYLAND
   case VK_ICD_WSI_PLATFORM_WAYLAND:
      return &wayland::surface_properties::get_instance();
#endif
#if BUILD_WSI_DISPLAY
   case VK_ICD_WSI_PLATFORM_DISPLAY:
      return &display::surface_properties::get_instance();
#endif
#if BUILD_WSI_X11
   case VK_ICD_WSI_PLATFORM_XCB:
   case VK_ICD_WSI_PLATFORM_XLIB:
      return &x11::surface_properties::get_instance();
#endif
   default:
      return nullptr;
   }
}

surface_properties *get_surface_properties(layer::instance_private_data &instance_data, VkSurfaceKHR surface)
{
   auto *wsi_surface = instance_data.get_surface(surface);

   if (wsi_surface)
   {
      return &wsi_surface->get_properties();
   }

   return nullptr;
}

util::unique_ptr<swapchain_base> allocate_surface_swapchain(VkSurfaceKHR surface, layer::device_private_data &dev_data,
                                                            const VkAllocationCallbacks *pAllocator)
{
   wsi::surface *wsi_surface = dev_data.instance_data.get_surface(surface);
   if (wsi_surface)
   {
      return wsi_surface->allocate_swapchain(dev_data, pAllocator);
   }
   return nullptr;
}

util::wsi_platform_set find_enabled_layer_platforms(const VkInstanceCreateInfo *pCreateInfo)
{
   util::wsi_platform_set ret;
   for (const auto &ext_provided_by_layer : supported_wsi_extensions)
   {
      for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
      {
         const char *ext_requested_by_user = pCreateInfo->ppEnabledExtensionNames[i];
         if (strcmp(ext_requested_by_user, ext_provided_by_layer.extension.extensionName) == 0)
         {
            ret.add(ext_provided_by_layer.platform);
         }
      }
   }
   return ret;
}

static VkResult get_available_device_extensions(VkPhysicalDevice physical_device,
                                                util::extension_list &available_extensions)
{
   auto &instance_data = layer::instance_private_data::get(physical_device);
   util::vector<VkExtensionProperties> properties{ available_extensions.get_allocator() };
   uint32_t count = 0;
   TRY_LOG(instance_data.disp.EnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr),
           "Failed to enumurate properties of available physical device extensions");

   if (!properties.try_resize(count))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG(instance_data.disp.EnumerateDeviceExtensionProperties(physical_device, nullptr, &count, properties.data()),
           "Failed to enumurate properties of available physical device extensions");
   TRY_LOG_CALL(available_extensions.add(properties.data(), count));

   return VK_SUCCESS;
}

VkResult add_device_extensions_required_by_layer(VkPhysicalDevice phys_dev,
                                                 const util::wsi_platform_set enabled_platforms,
                                                 util::extension_list &extensions_to_enable)
{
   util::allocator allocator{ extensions_to_enable.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND };

   util::extension_list available_device_extensions{ allocator };
   TRY_LOG(get_available_device_extensions(phys_dev, available_device_extensions),
           "Failed to acquire available device extensions");

   /* Add optional extensions independent of winsys. */
   {
      const char *optional_extensions[] = {
         VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
         VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
         VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
         VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#if ENABLE_INSTRUMENTATION
         VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME,
#endif
      };

      for (auto extension : optional_extensions)
      {
         if (available_device_extensions.contains(extension))
         {
            TRY_LOG_CALL(extensions_to_enable.add(extension));
         }
      }
   }

   for (const auto &wsi_ext : supported_wsi_extensions)
   {
      /* Skip iterating over platforms not enabled in the instance. */
      if (!enabled_platforms.contains(wsi_ext.platform))
      {
         continue;
      }

      util::extension_list extensions_required_by_layer{ allocator };
      surface_properties *props = get_surface_properties(wsi_ext.platform);
      if (props == nullptr)
      {
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      TRY_LOG(props->get_required_device_extensions(extensions_required_by_layer),
              "Failed to acquire required device extensions");

      bool supported = available_device_extensions.contains(extensions_required_by_layer);
      if (!supported)
      {
         /* Can we accept failure? The layer unconditionally advertises support for this platform and the loader uses
          * this information to enable its own support of the vkCreate*SurfaceKHR entrypoints. The rest of the Vulkan
          * stack may not support this extension so we cannot blindly fall back to it.
          * For now treat this as an error.
          */
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      TRY_LOG_CALL(extensions_to_enable.add(extensions_required_by_layer));
   }

   return VK_SUCCESS;
}

VkResult add_instance_extensions_required_by_layer(const util::wsi_platform_set enabled_platforms,
                                                   util::extension_list &extensions_to_enable)
{
   util::allocator allocator{ extensions_to_enable.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND };
   /* Requesting available instance extensions (as it happens with the device)
    * before adding additional ones isn't supported during the instance creation.
    * The reason for that is that the vulkan loader doesn't support layers to call vkEnumerateInstanceExtensionProperties.
    */
   for (const auto &wsi_ext : supported_wsi_extensions)
   {
      /* Skip iterating over platforms not enabled in the instance. */
      if (!enabled_platforms.contains(wsi_ext.platform))
      {
         continue;
      }

      util::extension_list extensions_required_by_layer{ allocator };
      auto *props = get_surface_properties(wsi_ext.platform);
      if (props == nullptr)
      {
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      TRY_LOG(props->get_required_instance_extensions(extensions_required_by_layer),
              "Failed to acquire required instance extensions");

      TRY_LOG_CALL(extensions_to_enable.add(extensions_required_by_layer));
   }

   return VK_SUCCESS;
}

void destroy_surface_swapchain(swapchain_base *swapchain, layer::device_private_data &dev_data,
                               const VkAllocationCallbacks *pAllocator)
{
   assert(swapchain);

   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pAllocator };
   alloc.destroy(1, swapchain);
}

PFN_vkVoidFunction get_proc_addr(const char *name, const layer::instance_private_data &instance_data)
{
   /*
    * Note that we here assume that there are no two get_proc_addr implementations
    * that handle the same function name.
    */
   for (const auto &wsi_ext : supported_wsi_extensions)
   {
      surface_properties *props = get_surface_properties(wsi_ext.platform);
      if (props == nullptr)
      {
         return nullptr;
      }

      PFN_vkVoidFunction func = props->get_proc_addr(name);
      if (props->is_surface_extension_enabled(instance_data) && func)
      {
         return func;
      }
   }
   return nullptr;
}

} // namespace wsi
