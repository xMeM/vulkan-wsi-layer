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

#include "wl_helpers.hpp"

#include <cstring>
#include <memory>
#include <poll.h>
#include <errno.h>
#include <cassert>

#include "wl_object_owner.hpp"

struct formats_vector
{
   util::vector<drm_format_pair> *formats{nullptr};
   bool is_out_of_memory{false};
};

namespace
{
   /* Handler for format event of the zwp_linux_dmabuf_v1 interface. */
   extern "C" void dma_buf_format_handler(void *data,
                                          struct zwp_linux_dmabuf_v1 *dma_buf,
                                          uint32_t drm_format) {}

   /* Handler for modifier event of the zwp_linux_dmabuf_v1 interface. */
   extern "C" void dma_buf_modifier_handler(void *data,
                                            struct zwp_linux_dmabuf_v1 *dma_buf,
                                            uint32_t drm_format, uint32_t modifier_hi,
                                            uint32_t modifier_low)
   {
      auto *drm_supported_formats = reinterpret_cast<formats_vector *>(data);

      drm_format_pair format = {};
      format.fourcc = drm_format;
      format.modifier = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_low;

      if (!drm_supported_formats->formats->try_push_back(format))
      {
         drm_supported_formats->is_out_of_memory = true;
      }
   }
}

VkResult get_supported_formats_and_modifiers(
   wl_display* display, zwp_linux_dmabuf_v1 *dmabuf_interface,
   util::vector<drm_format_pair> &supported_formats)
{
   formats_vector drm_supported_formats;
   drm_supported_formats.formats = &supported_formats;

   const zwp_linux_dmabuf_v1_listener dma_buf_listener = {
      .format = dma_buf_format_handler, .modifier = dma_buf_modifier_handler,
   };
   int res = zwp_linux_dmabuf_v1_add_listener(dmabuf_interface, &dma_buf_listener,
                                              &drm_supported_formats);
   if (res < 0)
   {
      WSI_PRINT_ERROR("Failed to add zwp_linux_dmabuf_v1 listener.\n");
      return VK_ERROR_UNKNOWN;
   }

   /* Get all modifier events. */
   res = wl_display_roundtrip(display);
   if (res < 0)
   {
      WSI_PRINT_ERROR("Roundtrip failed.\n");
      return VK_ERROR_UNKNOWN;
   }

   if (drm_supported_formats.is_out_of_memory)
   {
      WSI_PRINT_ERROR("Host got out of memory.\n");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

extern "C" {

   void registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
                         uint32_t version)
   {
      auto dmabuf_interface = reinterpret_cast<wsi::wayland::zwp_linux_dmabuf_v1_owner* >(data);

      if (!strcmp(interface, "zwp_linux_dmabuf_v1"))
      {
         version = ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION;
         zwp_linux_dmabuf_v1 *dmabuf_interface_obj =
            reinterpret_cast<zwp_linux_dmabuf_v1 *>(wl_registry_bind(
               wl_registry, name, &zwp_linux_dmabuf_v1_interface, version));

         if (dmabuf_interface_obj == nullptr)
         {
            WSI_PRINT_ERROR("Failed to get zwp_linux_dmabuf_v1 interface.\n");
            return;
         }

         dmabuf_interface->reset(dmabuf_interface_obj);
      }
   }

   int dispatch_queue(struct wl_display *display, struct wl_event_queue *queue, int timeout)
   {
      int err;
      struct pollfd pfd = {};
      int retval;

      /* Before we sleep, dispatch any pending events. prepare_read_queue will return 0 whilst there are pending
       * events to dispatch on the queue. */
      while (0 != wl_display_prepare_read_queue(display, queue))
      {
         /* dispatch_queue_pending returns -1 on error, or the number of events dispatched otherwise. If we
          * already dispatched some events, then we might not need to sleep, as we might have just dispatched
          * the event we want, so return immediately. */
         err = wl_display_dispatch_queue_pending(display, queue);
         if (err)
         {
            return (0 > err) ? -1 : 1;
         }
      }

      /* wl_display_read_events performs a non-blocking read. */
      pfd.fd = wl_display_get_fd(display);
      pfd.events = POLLIN;
      while (true)
      {
         /* Timeout is given in milliseconds. A return value of 0, or -1 with errno set to EINTR means that we
          * should retry as the timeout was exceeded or we were interrupted by a signal, respectively. A
          * return value of 1 means that something happened, and we should inspect the pollfd structure to see
          * just what that was.
          */
         err = poll(&pfd, 1, timeout);
         if (0 == err)
         {
            /* Timeout. */
            wl_display_cancel_read(display);
            return 0;
         }
         else if (-1 == err)
         {
            if (EINTR == errno)
            {
               /* Interrupted by a signal; restart. This resets the timeout. */
               continue;
            }
            else
            {
               /* Something else bad happened; abort. */
               wl_display_cancel_read(display);
               return -1;
            }
         }
         else
         {
            if (POLLIN == pfd.revents)
            {
               /* We have data to read, and no errors; proceed to read_events. */
               break;
            }
            else
            {
               /* An error occurred, e.g. file descriptor was closed from underneath us. */
               wl_display_cancel_read(display);
               return -1;
            }
         }
      }

      /* Actually read the events from the display. A failure in read_events calls cancel_read internally for us,
       * so we don't need to do that here. */
      err = wl_display_read_events(display);
      if (0 != err)
      {
         return -1;
      }

      /* Finally, if we read any events relevant to our queue, we can dispatch them. */
      err = wl_display_dispatch_queue_pending(display, queue);
      retval = err < 0 ? -1 : 1;

      return retval;
   }
}
