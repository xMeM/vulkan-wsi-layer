/*
 * Copyright (c) 2021, 2024 Arm Limited.
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
   util::vector<drm_format_pair> *formats{ nullptr };
   bool is_out_of_memory{ false };
};

namespace
{
/* Handler for format event of the zwp_linux_dmabuf_v1 interface. */
VWL_CAPI_CALL(void)
zwp_linux_dmabuf_v1_format_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format) VWL_API_POST
{
}

/* Handler for modifier event of the zwp_linux_dmabuf_v1 interface. */
VWL_CAPI_CALL(void)
zwp_linux_dmabuf_v1_modifier_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format,
                                  uint32_t modifier_hi, uint32_t modifier_low) VWL_API_POST
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
   const util::allocator &allocator;
   wl_display *display;
   wl_surface *surf;
};

surface::surface(const init_parameters &params)
   : wsi::surface()
   , wayland_display(params.display)
   , wayland_surface(params.surf)
   , supported_formats(params.allocator)
   , properties(this, params.allocator)
   , surface_queue(nullptr)
   , last_frame_callback(nullptr)
   , present_pending(false)
{
}

VWL_CAPI_CALL(void)
surface_registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
                         uint32_t version) VWL_API_POST
{
   auto wsi_surface = reinterpret_cast<wsi::wayland::surface *>(data);

   if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name) && version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
   {
      zwp_linux_dmabuf_v1 *dmabuf_interface_obj = reinterpret_cast<zwp_linux_dmabuf_v1 *>(wl_registry_bind(
         wl_registry, name, &zwp_linux_dmabuf_v1_interface, ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION));

      if (dmabuf_interface_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to get zwp_linux_dmabuf_v1 interface.");
         return;
      }

      wsi_surface->dmabuf_interface.reset(dmabuf_interface_obj);
   }
   else if (!strcmp(interface, zwp_linux_explicit_synchronization_v1_interface.name))
   {
      zwp_linux_explicit_synchronization_v1 *explicit_sync_interface_obj =
         reinterpret_cast<zwp_linux_explicit_synchronization_v1 *>(
            wl_registry_bind(wl_registry, name, &zwp_linux_explicit_synchronization_v1_interface, 1));

      if (explicit_sync_interface_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to get zwp_linux_explicit_synchronization_v1 interface.");
         return;
      }

      wsi_surface->explicit_sync_interface.reset(explicit_sync_interface_obj);
   }
}

bool surface::init()
{
   surface_queue.reset(wl_display_create_queue(wayland_display));
   if (surface_queue.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl surface queue.");
      return false;
   }

   auto display_proxy = make_proxy_with_queue(wayland_display, surface_queue.get());
   if (display_proxy == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl display proxy.");
      return false;
   };

   auto registry = wayland_owner<wl_registry>{ wl_display_get_registry(display_proxy.get()) };
   if (registry == nullptr)
   {
      WSI_LOG_ERROR("Failed to get wl display registry.");
      return false;
   }

   const wl_registry_listener registry_listener = { surface_registry_handler };
   int res = wl_registry_add_listener(registry.get(), &registry_listener, this);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add registry listener.");
      return false;
   }

   res = wl_display_roundtrip_queue(wayland_display, surface_queue.get());
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

   if (explicit_sync_interface.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to obtain zwp_linux_explicit_synchronization_v1 interface.");
      return false;
   }

   auto surface_sync_obj =
      zwp_linux_explicit_synchronization_v1_get_synchronization(explicit_sync_interface.get(), wayland_surface);
   if (surface_sync_obj == nullptr)
   {
      WSI_LOG_ERROR("Failed to retrieve surface synchronization interface");
      return false;
   }

   surface_sync_interface.reset(surface_sync_obj);

   VkResult vk_res = get_supported_formats_and_modifiers(wayland_display, surface_queue.get(), dmabuf_interface.get(),
                                                         supported_formats);
   if (vk_res != VK_SUCCESS)
   {
      return false;
   }

   return true;
}

util::unique_ptr<surface> surface::make_surface(const util::allocator &allocator, wl_display *display, wl_surface *surf)
{
   init_parameters params{ allocator, display, surf };
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

static void frame_done(void *data, wl_callback *cb, uint32_t time)
{
   (void)time;

   bool *present_pending = reinterpret_cast<bool *>(data);
   assert(present_pending);

   *present_pending = false;
}

bool surface::set_frame_callback()
{
   /* request a hint when we can present the _next_ frame */
   auto surface_proxy = make_proxy_with_queue(wayland_surface, surface_queue.get());
   if (surface_proxy == nullptr)
   {
      WSI_LOG_ERROR("failed to create wl_surface proxy");
      return false;
   }

   /* Reset will also destroy the previous callback object. */
   last_frame_callback.reset(wl_surface_frame(surface_proxy.get()));
   if (last_frame_callback.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to create frame callback.");
      return false;
   }

   static const wl_callback_listener frame_listener = { frame_done };
   present_pending = true;
   int res = wl_callback_add_listener(last_frame_callback.get(), &frame_listener, &present_pending);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add frame done callback listener.");
      return false;
   }

   return true;
}

bool surface::wait_next_frame_event()
{
   /*
    * In a previous present call we sent a wl_surface::frame request, which will
    * trigger an event when the compositor starts a redraw using the previous frame
    * we sent. If the compositor isn't sending us frame events at least every second
    * we don't wait indefinitely so we don't block the next image presentation if
    * we are, e.g. minimised.
    */
   const int timeout = 1000;
   while (present_pending)
   {
      int res = dispatch_queue(wayland_display, surface_queue.get(), timeout);
      if (res < 0)
      {
         WSI_LOG_ERROR("Error while waiting for the compositor to send the next frame event.");
         return false;
      }
      else if (res == 0)
      {
         WSI_LOG_INFO("Wait for frame event timed out, present anyway.");
         present_pending = false;
      }
   }

   return true;
}

} // namespace wayland
} // namespace wsi
