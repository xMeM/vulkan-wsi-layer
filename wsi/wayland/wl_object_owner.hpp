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

#pragma once

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0
#endif
#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include <linux-explicit-synchronization-unstable-v1-protocol.h>
#include <presentation-time-client-protocol.h>
#include <memory.h>
#include <functional>

namespace wsi
{
namespace wayland
{

static inline void wayland_object_destroy(wl_registry *obj)
{
   wl_registry_destroy(obj);
}

static inline void wayland_object_destroy(zwp_linux_dmabuf_v1 *obj)
{
   zwp_linux_dmabuf_v1_destroy(obj);
}

static inline void wayland_object_destroy(zwp_linux_explicit_synchronization_v1 *obj)
{
   zwp_linux_explicit_synchronization_v1_destroy(obj);
}

static inline void wayland_object_destroy(zwp_linux_surface_synchronization_v1 *obj)
{
   zwp_linux_surface_synchronization_v1_destroy(obj);
}

static inline void wayland_object_destroy(wp_presentation *obj)
{
   wp_presentation_destroy(obj);
}

static inline void wayland_object_destroy(wl_callback *obj)
{
   wl_callback_destroy(obj);
}

static inline void wayland_object_destroy(wl_event_queue *obj)
{
   wl_event_queue_destroy(obj);
}

template <typename T>
struct wayland_deleter
{
   void operator()(T *obj) const
   {
      if (obj != nullptr)
      {
         wayland_object_destroy(obj);
      }
   }
};

template <typename T>
using wayland_owner = std::unique_ptr<T, wayland_deleter<T>>;

template <typename T>
static std::unique_ptr<T, std::function<void(T *)>> make_proxy_with_queue(T *object, wl_event_queue *queue)
{
   auto proxy = reinterpret_cast<T *>(wl_proxy_create_wrapper(object));
   if (proxy != nullptr)
   {
      wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(proxy), queue);
   }

   auto delete_proxy = [](T *proxy) { wl_proxy_wrapper_destroy(reinterpret_cast<wl_proxy *>(proxy)); };

   return std::unique_ptr<T, std::function<void(T *)>>(proxy, delete_proxy);
}

} // namespace wayland
} // namespace wsi
