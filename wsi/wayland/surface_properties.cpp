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

#define VK_USE_PLATFORM_WAYLAND_KHR 1

#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>

#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include "surface_properties.hpp"
#include "surface.hpp"
#include "layer/private_data.hpp"
#include "wl_helpers.hpp"
#include "wl_object_owner.hpp"
#include "util/drm/drm_utils.hpp"
#include "util/log.hpp"

#define NELEMS(x) (sizeof(x) / sizeof(x[0]))

namespace wsi
{
namespace wayland
{

surface_properties::surface_properties(surface &wsi_surface, const util::allocator &allocator)
   : specific_surface(&wsi_surface)
   , supported_formats(allocator)
{
}

surface_properties::surface_properties()
   : specific_surface(nullptr)
   , supported_formats(util::allocator::get_generic())
{
}

surface_properties &surface_properties::get_instance()
{
   static surface_properties instance;
   return instance;
}

VkResult surface_properties::get_surface_capabilities(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                      VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   /* Image count limits */
   pSurfaceCapabilities->minImageCount = 2;
   pSurfaceCapabilities->maxImageCount = MAX_SWAPCHAIN_IMAGE_COUNT;

   /* Surface extents */
   pSurfaceCapabilities->currentExtent = { 0xffffffff, 0xffffffff };
   pSurfaceCapabilities->minImageExtent = { 1, 1 };

   /* TODO: Ask the device for max - for now setting the max from the GPU, may be ask the display somehow*/
   VkPhysicalDeviceProperties dev_props;
   layer::instance_private_data::get(physical_device).disp.GetPhysicalDeviceProperties(physical_device, &dev_props);

   pSurfaceCapabilities->maxImageExtent = { dev_props.limits.maxImageDimension2D,
                                            dev_props.limits.maxImageDimension2D };
   pSurfaceCapabilities->maxImageArrayLayers = 1;

   /* Surface transforms */
   pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

   /* TODO: Composite alpha */
   pSurfaceCapabilities->supportedCompositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);

   /* Image usage flags */
   pSurfaceCapabilities->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

static VkResult get_vk_supported_formats(const util::vector<drm_format_pair> &drm_supported_formats,
                                         vk_format_set &vk_supported_formats)
{
   for (const auto &drm_format : drm_supported_formats)
   {
      const VkFormat vk_format = util::drm::drm_to_vk_format(drm_format.fourcc);
      if (vk_format != VK_FORMAT_UNDEFINED)
      {
         auto it = vk_supported_formats.try_insert(vk_format);
         if (!it.has_value())
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }
      const VkFormat srgb_vk_format = util::drm::drm_to_vk_srgb_format(drm_format.fourcc);
      if (srgb_vk_format != VK_FORMAT_UNDEFINED)
      {
         auto it = vk_supported_formats.try_insert(srgb_vk_format);
         if (!it.has_value())
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }
   }
   return VK_SUCCESS;
}

VkResult surface_properties::get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                 uint32_t *surfaceFormatCount, VkSurfaceFormatKHR *surfaceFormats)
{
   assert(specific_surface);
   if (!supported_formats.size())
   {
      VkResult res = get_vk_supported_formats(specific_surface->get_formats(), supported_formats);
      if (res != VK_SUCCESS)
      {
         return res;
      }
   }

   assert(surfaceFormatCount != nullptr);
   if (nullptr == surfaceFormats)
   {
      *surfaceFormatCount = supported_formats.size();
      return VK_SUCCESS;
   }

   VkResult res = VK_SUCCESS;

   if (supported_formats.size() > *surfaceFormatCount)
   {
      res = VK_INCOMPLETE;
   }

   uint32_t format_count = 0;
   for (const auto &format : supported_formats)
   {
      if (format_count >= *surfaceFormatCount)
      {
         break;
      }
      surfaceFormats[format_count].format = format;
      surfaceFormats[format_count++].colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
   }
   *surfaceFormatCount = format_count;

   return res;
}

VkResult surface_properties::get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                       uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes)
{

   VkResult res = VK_SUCCESS;

   static std::array<const VkPresentModeKHR, 2> modes = {
      VK_PRESENT_MODE_FIFO_KHR,
      VK_PRESENT_MODE_MAILBOX_KHR,
   };

   assert(pPresentModeCount != nullptr);

   if (nullptr == pPresentModes)
   {
      *pPresentModeCount = modes.size();
   }
   else
   {
      if (modes.size() > *pPresentModeCount)
      {
         res = VK_INCOMPLETE;
      }
      *pPresentModeCount = std::min(*pPresentModeCount, static_cast<uint32_t>(modes.size()));
      for (uint32_t i = 0; i < *pPresentModeCount; ++i)
      {
         pPresentModes[i] = modes[i];
      }
   }

   return res;
}

static const char *required_device_extensions[] = {
   VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
   VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
   VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
   VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
   VK_KHR_MAINTENANCE1_EXTENSION_NAME,
   VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
   VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
   VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
   VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
   VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
   VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
};

VkResult surface_properties::get_required_device_extensions(util::extension_list &extension_list)
{
   return extension_list.add(required_device_extensions, NELEMS(required_device_extensions));
}

/* TODO: Check for zwp_linux_dmabuf_v1 protocol in display */
VkBool32 GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physical_device, uint32_t queue_index,
                                                        struct wl_display *display)
{
   bool dev_supports_sync =
      sync_fd_fence_sync::is_supported(layer::instance_private_data::get(physical_device), physical_device);
   if (!dev_supports_sync)
   {
      return VK_FALSE;
   }

   return VK_TRUE;
}

extern "C" VkResult CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                                            const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
   auto &instance_data = layer::instance_private_data::get(instance);
   util::allocator allocator{ instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pAllocator };
   auto wsi_surface = surface::make_surface(allocator, pCreateInfo->display, pCreateInfo->surface);
   if (wsi_surface == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   VkResult res = instance_data.disp.CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
   if (res == VK_SUCCESS)
   {
      auto surface_base = util::unique_ptr<wsi::surface>(std::move(wsi_surface));
      res = instance_data.add_surface(*pSurface, surface_base);
      if (res != VK_SUCCESS)
      {
         instance_data.disp.DestroySurfaceKHR(instance, *pSurface, pAllocator);
      }
   }
   return res;
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{
   if (strcmp(name, "vkGetPhysicalDeviceWaylandPresentationSupportKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceWaylandPresentationSupportKHR);
   }
   else if (strcmp(name, "vkCreateWaylandSurfaceKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateWaylandSurfaceKHR);
   }
   return nullptr;
}

} // namespace wayland
} // namespace wsi
