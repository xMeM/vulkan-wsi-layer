/*
 * Copyright (c) 2019-2021 Arm Limited.
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
#include "headless/surface_properties.hpp"
#include "headless/swapchain.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

#include <vulkan/vk_icd.h>

#if BUILD_WSI_WAYLAND
#include <vulkan/vulkan_wayland.h>
#include "wayland/surface_properties.hpp"
#include "wayland/swapchain.hpp"
#endif

namespace wsi
{

static struct wsi_extension
{
   VkExtensionProperties extension;
   VkIcdWsiPlatform platform;
} const supported_wsi_extensions[] = {
   { { VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_HEADLESS },
#if BUILD_WSI_WAYLAND
   { { VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_WAYLAND },
#endif
};

static surface_properties *get_surface_properties(VkIcdWsiPlatform platform)
{
   switch (platform)
   {
   case VK_ICD_WSI_PLATFORM_HEADLESS:
      return &headless::surface_properties::get_instance();
#if BUILD_WSI_WAYLAND
   case VK_ICD_WSI_PLATFORM_WAYLAND:
      return &wayland::surface_properties::get_instance();
#endif
   default:
      return nullptr;
   }
}

surface_properties *get_surface_properties(VkSurfaceKHR surface)
{
   VkIcdSurfaceBase *surface_base = reinterpret_cast<VkIcdSurfaceBase *>(surface);

   return get_surface_properties(surface_base->platform);
}

template <typename swapchain_type>
static swapchain_base *allocate_swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE, pAllocator };
   return alloc.create<swapchain_type>(1, dev_data, pAllocator);
}

swapchain_base *allocate_surface_swapchain(VkSurfaceKHR surface, layer::device_private_data &dev_data,
                                           const VkAllocationCallbacks *pAllocator)
{
   VkIcdSurfaceBase *surface_base = reinterpret_cast<VkIcdSurfaceBase *>(surface);

   switch (surface_base->platform)
   {
   case VK_ICD_WSI_PLATFORM_HEADLESS:
      return allocate_swapchain<wsi::headless::swapchain>(dev_data, pAllocator);
#if BUILD_WSI_WAYLAND
   case VK_ICD_WSI_PLATFORM_WAYLAND:
      return allocate_swapchain<wsi::wayland::swapchain>(dev_data, pAllocator);
#endif
   default:
      return nullptr;
   }
}

util::wsi_platform_set find_enabled_layer_platforms(const VkInstanceCreateInfo *pCreateInfo)
{
   util::wsi_platform_set ret;
   for (const auto &ext_provided_by_layer : supported_wsi_extensions)
   {
      for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
      {
         const char* ext_requested_by_user = pCreateInfo->ppEnabledExtensionNames[i];
         if (strcmp(ext_requested_by_user, ext_provided_by_layer.extension.extensionName) == 0)
         {
            ret.add(ext_provided_by_layer.platform);
         }
      }
   }
   return ret;
}

VkResult add_extensions_required_by_layer(VkPhysicalDevice phys_dev, const util::wsi_platform_set enabled_platforms,
                                          util::extension_list &extensions_to_enable)
{
   util::allocator allocator{extensions_to_enable.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND};
   util::extension_list device_extensions{allocator};

   util::vector<VkExtensionProperties> ext_props{allocator};
   layer::instance_private_data &inst_data = layer::instance_private_data::get(phys_dev);
   uint32_t count;
   VkResult res = inst_data.disp.EnumerateDeviceExtensionProperties(phys_dev, nullptr, &count, nullptr);

   if (res == VK_SUCCESS)
   {
      if (!ext_props.try_resize(count))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      res = inst_data.disp.EnumerateDeviceExtensionProperties(phys_dev, nullptr, &count, ext_props.data());
   }

   if (res != VK_SUCCESS)
   {
      return res;
   }

   res = device_extensions.add(ext_props.data(), count);

   if (res != VK_SUCCESS)
   {
      return res;
   }

   for (const auto &wsi_ext : supported_wsi_extensions)
   {
      /* Skip iterating over platforms not enabled in the instance. */
      if (!enabled_platforms.contains(wsi_ext.platform))
      {
         continue;
      }

      util::extension_list extensions_required_by_layer{allocator};
      surface_properties *props = get_surface_properties(wsi_ext.platform);
      res = props->get_required_device_extensions(extensions_required_by_layer);
      if (res != VK_SUCCESS)
      {
         return res;
      }

      bool supported = device_extensions.contains(extensions_required_by_layer);
      if (!supported)
      {
         /* Can we accept failure? The layer unconditionally advertises support for this platform and the loader uses
          * this information to enable its own support of the vkCreate*SurfaceKHR entrypoints. The rest of the Vulkan
          * stack may not support this extension so we cannot blindly fall back to it.
          * For now treat this as an error.
          */
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      res = extensions_to_enable.add(extensions_required_by_layer);
      if (res != VK_SUCCESS)
      {
         return res;
      }
   }
   return VK_SUCCESS;
}

void destroy_surface_swapchain(swapchain_base *swapchain, layer::device_private_data &dev_data,
                               const VkAllocationCallbacks *pAllocator)
{
   assert(swapchain);

   util::allocator alloc{ swapchain->get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE, pAllocator };
   alloc.destroy(1, swapchain);
}

PFN_vkVoidFunction get_proc_addr(const char *name)
{
   /*
    * Note that we here assume that there are no two get_proc_addr implementations
    * that handle the same function name.
    */
   for (const auto &wsi_ext : supported_wsi_extensions)
   {
      PFN_vkVoidFunction func = get_surface_properties(wsi_ext.platform)->get_proc_addr(name);
      if (func)
      {
         return func;
      }
   }
   return nullptr;
}

} // namespace wsi
