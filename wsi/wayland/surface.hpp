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
 * @brief Definitions for a Wayland WSI Surface
 */

#pragma once

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0
#endif
#include <wayland-client.h>

#include "wsi/surface.hpp"
#include "surface_properties.hpp"
#include "wl_object_owner.hpp"
#include "util/macros.hpp"

namespace wsi
{
namespace wayland
{

/**
 * Wayland callback for global wl_registry events to handle global objects required by @ref wsi::wayland::surface
 */
VWL_CAPI_CALL(void)
surface_registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
                         uint32_t version) VWL_API_POST;

class surface : public wsi::surface
{
public:
   surface() = delete;
   struct init_parameters;

   /** Constructor to allow for custom allocation, but require privately defined arguments. */
   surface(const init_parameters &);

   /**
    * @brief Allocates and initializes a surface
    *
    * @param allocator An allocator to use for host allocations needed for the surface.
    * @param display   The Wayland display used to create the VkSurface
    * @param surf      The Wayland surface used to create the VkSurface
    *
    * @return A constructed and initalized surface or nullptr on failure
    */
   static util::unique_ptr<surface> make_surface(const util::allocator &allocator, wl_display *display,
                                                 wl_surface *surf);

   /** Destructor */
   ~surface() override;

   wsi::surface_properties &get_properties() override;
   util::unique_ptr<swapchain_base> allocate_swapchain(layer::device_private_data &dev_data,
                                                       const VkAllocationCallbacks *allocator) override;

   /** Returns the Wayland display */
   wl_display *get_wl_display() const
   {
      return wayland_display;
   }

   /** Returns the Wayland surface */
   wl_surface *get_wl_surface() const
   {
      return wayland_surface;
   }

   /**
    * @brief Returns a pointer to the Wayland zwp_linux_dmabuf_v1 interface.
    *
    * The raw pointer is valid throughout the lifetime of this surface.
    */
   zwp_linux_dmabuf_v1 *get_dmabuf_interface()
   {
      return dmabuf_interface.get();
   }

   /**
    * @brief Returns a pointer to the Wayland zwp_linux_surface_synchronization_v1 interface obtained for the wayland
    *        surface.
    *
    * The raw pointer is valid for the lifetime of the surface.
    */
   zwp_linux_surface_synchronization_v1 *get_surface_sync_interface()
   {
      return surface_sync_interface.get();
   }

   /**
    * @brief Returns a reference to a list of DRM formats supported by the Wayland surface.
    *
    * The reference is valid throughout the lifetime of this surface.
    */
   const util::vector<drm_format_pair> &get_formats() const
   {
      return supported_formats;
   }

   /**
    * @brief Set the next frame callback.
    *
    * Make a frame request on the compositor which will be applied in the next
    * wl_surface::commit. It overwrites previous requested frame events.
    *
    * @return true for success, false otherwise.
    */
   bool set_frame_callback();

   /**
    * @brief Wait for the compositor's last requested frame event.
    *
    * @return true for success, false otherwise.
    */
   bool wait_next_frame_event();

private:
   /**
    * @brief Initialize the WSI surface by creating Wayland queues and linking to Wayland protocols.
    *
    * @return true on success, false otherwise.
    */
   bool init();

   friend void surface_registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name,
                                        const char *interface, uint32_t version) VWL_API_POST;

   /** The native Wayland display */
   wl_display *wayland_display;

   /**
    * Container for a private queue for surface events generated by the layer.
    * The queue is also used for dispatching frame callback events.
    * It should be destroyed after the objects that attached to it.
    */
   wayland_owner<wl_event_queue> surface_queue;

   /** The native Wayland surface */
   wl_surface *wayland_surface;
   /** A list of DRM formats supported by the Wayland compositor on this surface */
   util::vector<drm_format_pair> supported_formats;
   /** Surface properties specific to the Wayland surface. */
   surface_properties properties;

   /** Container for the zwp_linux_dmabuf_v1 interface binding */
   wayland_owner<zwp_linux_dmabuf_v1> dmabuf_interface;

   /** Container for the zwp_linux_explicit_synchronization_v1 interface binding */
   wayland_owner<zwp_linux_explicit_synchronization_v1> explicit_sync_interface;
   /** Container for the surface specific zwp_linux_surface_synchronization_v1 interface. */
   wayland_owner<zwp_linux_surface_synchronization_v1> surface_sync_interface;

   /** Container for the wp_presentation interface binding */
   wayland_owner<wp_presentation> presentation_time_interface;

   /**
    * Container for a callback object for the latest frame done event.
    *
    * The callback object should be destroyed before the queue so any new events
    * on the queue will be discarded. If a proxy object is destroyed after a queue,
    * it is possible in the meantime for a new event to arrive and processed.
    * This will result in a use after free error.
    */
   wayland_owner<wl_callback> last_frame_callback;

   /**
    * @brief true when waiting for the server hint to present a buffer
    *
    * true if a buffer has been presented and we've not had a wl_surface::frame
    * callback to indicate the server is ready for the next buffer.
    */
   bool present_pending;
};

} // namespace wayland
} // namespace wsi
