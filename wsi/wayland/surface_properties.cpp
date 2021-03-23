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

struct vk_format_hasher
{
   size_t operator()(const VkFormat format) const
   {
      return std::hash<uint64_t>()(static_cast<uint64_t>(format));
   }
};

using vk_format_set = std::unordered_set<VkFormat, vk_format_hasher>;

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
   /* There is no maximum theoretically speaking */
   pSurfaceCapabilities->maxImageCount = UINT32_MAX;

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

static void get_vk_supported_formats(const util::vector<drm_format_pair> &drm_supported_formats,
                                     vk_format_set &vk_supported_formats)
{
   for (const auto &drm_format : drm_supported_formats)
   {
      const VkFormat vk_format = util::drm::drm_to_vk_format(drm_format.fourcc);
      if (vk_format != VK_FORMAT_UNDEFINED)
      {
         const VkFormat srgb_vk_format = util::drm::drm_to_vk_srgb_format(drm_format.fourcc);
         if (srgb_vk_format != VK_FORMAT_UNDEFINED)
         {
            vk_supported_formats.insert({srgb_vk_format, vk_format});
         }
         else
         {
            vk_supported_formats.insert(vk_format);
         }
      }
   }
}

/*
 * @brief Query a surface's supported formats from the compositor.
 *
 * @details A wl_registry is created in order to get a zwp_linux_dmabuf_v1 object.
 * Then a listener is attached to that object in order to get the supported formats
 * from the server. The supported formats are stored in @p vk_supported_formats.
 *
 * @param[in]  surface                  The surface, which the supported formats
 *                                      are for.
 * @param[out] vk_supported_formats     unordered_set which will store the supported
 *                                      formats.
 *
 * @retval VK_SUCCESS                    Indicates success.
 * @retval VK_ERROR_SURFACE_LOST_KHR     Indicates one of the Wayland functions failed.
 * @retval VK_ERROR_OUT_OF_DEVICE_MEMORY Indicates the host went out of memory.
 */
static VkResult query_supported_formats(
   const VkSurfaceKHR surface, vk_format_set &vk_supported_formats)
{
   const VkIcdSurfaceWayland *vk_surf = reinterpret_cast<VkIcdSurfaceWayland *>(surface);
   wl_display *display = vk_surf->display;

   auto registry = registry_owner{wl_display_get_registry(display)};
   if (registry.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to get wl display registry.");
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   auto dmabuf_interface = zwp_linux_dmabuf_v1_owner{nullptr};
   const wl_registry_listener registry_listener = { registry_handler };
   int res = wl_registry_add_listener(registry.get(), &registry_listener, &dmabuf_interface);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add registry listener.");
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   /* Get the dma buf interface. */
   res = wl_display_roundtrip(display);
   if (res < 0)
   {
      WSI_LOG_ERROR("Roundtrip failed.");
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   if (dmabuf_interface.get() == nullptr)
   {
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   util::vector<drm_format_pair> drm_supported_formats(util::allocator::get_generic());
   const VkResult ret = get_supported_formats_and_modifiers(display, dmabuf_interface.get(), drm_supported_formats);
   if (ret != VK_SUCCESS)
   {
      return ret == VK_ERROR_UNKNOWN ? VK_ERROR_SURFACE_LOST_KHR : ret;
   }

   get_vk_supported_formats(drm_supported_formats, vk_supported_formats);

   return ret;
}

VkResult surface_properties::get_surface_formats(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                 uint32_t *surfaceFormatCount, VkSurfaceFormatKHR *surfaceFormats)
{
   vk_format_set formats;
   const auto query_res = query_supported_formats(surface, formats);
   if (query_res != VK_SUCCESS)
   {
      return query_res;
   }

   assert(surfaceFormatCount != nullptr);
   if (nullptr == surfaceFormats)
   {
      *surfaceFormatCount = formats.size();
      return VK_SUCCESS;
   }

   VkResult res = VK_SUCCESS;

   if (formats.size() > *surfaceFormatCount)
   {
      res = VK_INCOMPLETE;
   }

   uint32_t format_count = 0;
   for (const auto &format : formats)
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
};

static std::unique_ptr<util::extension_list> populate_device_extensions()
{
   std::unique_ptr<util::extension_list> ret(new util::extension_list(util::allocator::get_generic()));
   ret->add(required_device_extensions, NELEMS(required_device_extensions));

   return ret;
}

const util::extension_list &surface_properties::get_required_device_extensions()
{
   static std::unique_ptr<util::extension_list> device_extensions = populate_device_extensions();
   return *device_extensions;
}

bool surface_properties::physical_device_supported(VkPhysicalDevice dev)
{
   static util::extension_list device_extensions{util::allocator::get_generic()};
   device_extensions.add(dev);

   static util::extension_list required_extensions{util::allocator::get_generic()};
   required_extensions.add(required_device_extensions, NELEMS(required_device_extensions));

   return device_extensions.contains(required_extensions);
}

/* TODO: Check for zwp_linux_dmabuf_v1 protocol in display */
VkBool32 GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physical_device, uint32_t queue_index,
                                                        struct wl_display *display)
{
   return VK_TRUE;
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{
   if (strcmp(name, "vkGetPhysicalDeviceWaylandPresentationSupportKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceWaylandPresentationSupportKHR);
   }
   return nullptr;
}
} // namespace wayland
} // namespace wsi
