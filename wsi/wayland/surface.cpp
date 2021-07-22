/*
 * Copyright (c) 2021 Arm Limited.
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

/** @file
 * @brief Implementation of a Wayland WSI Surface
 */

#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"
#include "wl_object_owner.hpp"
#include "wl_helpers.hpp"
#include "util/log.hpp"

namespace wsi
{
namespace wayland
{

struct formats_vector
{
   util::vector<drm_format_pair> *formats{nullptr};
   bool is_out_of_memory{false};
};

namespace
{
/* Handler for format event of the zwp_linux_dmabuf_v1 interface. */
extern "C" void zwp_linux_dmabuf_v1_format_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format)
{
}

/* Handler for modifier event of the zwp_linux_dmabuf_v1 interface. */
extern "C" void zwp_linux_dmabuf_v1_modifier_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format,
                                                  uint32_t modifier_hi, uint32_t modifier_low)
{
   auto *drm_supported_formats = reinterpret_cast<formats_vector *>(data);

   drm_format_pair format = {};
   format.fourcc = drm_format;
   format.modifier = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_low;

   if (!drm_supported_formats->is_out_of_memory)
   {
      drm_supported_formats->is_out_of_memory = !drm_supported_formats->formats->try_push_back(format);
   }
}
} // namespace

/*
 * @brief Get supported formats and modifiers using the zwp_linux_dmabuf_v1 interface.
 *
 * @param[in]  display               The wl_display that is being used.
 * @param[in]  queue                 The wl_event_queue set for the @p dmabuf_interface
 * @param[in]  dmabuf_interface      Object of the zwp_linux_dmabuf_v1 interface.
 * @param[out] supported_formats     Vector which will contain the supported drm
 *                                   formats and their modifiers.
 *
 * @retval VK_SUCCESS                    Indicates success.
 * @retval VK_ERROR_UNKNOWN              Indicates one of the Wayland functions failed.
 * @retval VK_ERROR_OUT_OF_DEVICE_MEMORY Indicates the host went out of memory.
 */
static VkResult get_supported_formats_and_modifiers(wl_display *display, wl_event_queue *queue,
                                                    zwp_linux_dmabuf_v1 *dmabuf_interface,
                                                    util::vector<drm_format_pair> &supported_formats)
{
   formats_vector drm_supported_formats;
   drm_supported_formats.formats = &supported_formats;

   const zwp_linux_dmabuf_v1_listener dma_buf_listener = {
      .format = zwp_linux_dmabuf_v1_format_impl,
      .modifier = zwp_linux_dmabuf_v1_modifier_impl,
   };
   int res = zwp_linux_dmabuf_v1_add_listener(dmabuf_interface, &dma_buf_listener, &drm_supported_formats);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add zwp_linux_dmabuf_v1 listener.");
      return VK_ERROR_UNKNOWN;
   }

   /* Get all modifier events. */
   res = wl_display_roundtrip_queue(display, queue);
   if (res < 0)
   {
      WSI_LOG_ERROR("Roundtrip failed.");
      return VK_ERROR_UNKNOWN;
   }

   if (drm_supported_formats.is_out_of_memory)
   {
      WSI_LOG_ERROR("Host got out of memory.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

struct surface::init_parameters
{
   const util::allocator& allocator;
   wl_display *display;
   wl_surface *surf;
};

surface::surface(const init_parameters &params)
   : wsi::surface()
   , wayland_display(params.display)
   , wayland_surface(params.surf)
   , supported_formats(params.allocator)
   , properties(*this, params.allocator)
   , surface_queue(nullptr)
{
}

bool surface::init()
{
   surface_queue = wl_display_create_queue(wayland_display);

   if (surface_queue == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl surface queue.");
      return false;
   }

   auto display_proxy = make_proxy_with_queue(wayland_display, surface_queue);
   if (display_proxy == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl display proxy.");
      return false;
   };

   auto registry = registry_owner{ wl_display_get_registry(display_proxy.get()) };
   if (registry == nullptr)
   {
      WSI_LOG_ERROR("Failed to get wl display registry.");
      return false;
   }

   const wl_registry_listener registry_listener = { registry_handler };
   int res = wl_registry_add_listener(registry.get(), &registry_listener, &dmabuf_interface);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add registry listener.");
      return false;
   }

   res = wl_display_roundtrip_queue(wayland_display, surface_queue);
   if (res < 0)
   {
      WSI_LOG_ERROR("Roundtrip failed.");
      return false;
   }

   if (dmabuf_interface.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to obtain zwp_linux_dma_buf_v1 interface.");
      return false;
   }

   VkResult vk_res =
      get_supported_formats_and_modifiers(wayland_display, surface_queue, dmabuf_interface.get(), supported_formats);
   if (vk_res != VK_SUCCESS)
   {
      return false;
   }

   return true;
}

util::unique_ptr<surface> surface::make_surface(const util::allocator &allocator, wl_display *display,
                                                wl_surface *surf)
{
   init_parameters params {allocator, display, surf};
   auto wsi_surface = allocator.make_unique<surface>(params);
   if (wsi_surface != nullptr)
   {
      if (wsi_surface->init())
      {
         return wsi_surface;
      }
   }
   return nullptr;
}

surface::~surface()
{
   if (surface_queue != nullptr)
   {
      wl_event_queue_destroy(surface_queue);
   }
}

wsi::surface_properties &surface::get_properties()
{
   return properties;
}

util::unique_ptr<swapchain_base> surface::allocate_swapchain(layer::device_private_data &dev_data,
                                                             const VkAllocationCallbacks *allocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, allocator };
   return util::unique_ptr<swapchain_base>(alloc.make_unique<swapchain>(dev_data, allocator, *this));
}

} // namespace wayland
} // namespace wsi