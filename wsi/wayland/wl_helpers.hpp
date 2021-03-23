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

#pragma once

#include <stdint.h>
#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include "util/custom_allocator.hpp"
struct drm_format_pair
{
   uint32_t fourcc;
   uint64_t modifier;
};

/*
 * @brief Get supported formats and modifiers using the zwp_linux_dmabuf_v1 interface.
 *
 * @param[in]  display               The wl_display that is being used.
 * @param[in]  dmabuf_interface      Object of the zwp_linux_dmabuf_v1 interface.
 * @param[out] supported_formats     Vector which will contain the supported drm
 *                                   formats and their modifiers.
 *
 * @retval VK_SUCCESS                    Indicates success.
 * @retval VK_ERROR_UNKNOWN              Indicates one of the Wayland functions failed.
 * @retval VK_ERROR_OUT_OF_DEVICE_MEMORY Indicates the host went out of memory.
 */
VkResult get_supported_formats_and_modifiers(
   struct wl_display* display, struct zwp_linux_dmabuf_v1 *dmabuf_interface,
   util::vector<drm_format_pair> &supported_formats);

extern "C" {
   void registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
                         uint32_t version);

   /**
    * @brief Dispatch events from a Wayland event queue
    *
    * Dispatch events from a given Wayland display event queue, including calling event handlers, and flush out any
    * requests the event handlers may have written. Specification of a timeout allows the wait to be bounded. If any
    * events are already pending dispatch (have been read from the display by another thread or event queue), they
    * will be dispatched and the function will return immediately, without waiting for new events to arrive.
    *
    * @param  display Wayland display to dispatch events from
    * @param  queue   Event queue to dispatch events from; other event queues will not have their handlers called from
    *                 within this function
    * @param  timeout Maximum time to wait for events to arrive, in milliseconds
    * @return         1 if one or more events were dispatched on this queue, 0 if the timeout was reached without any
    *                 events being dispatched, or -1 on error.
    */
   int dispatch_queue(struct wl_display *display, struct wl_event_queue *queue, int timeout);
}
