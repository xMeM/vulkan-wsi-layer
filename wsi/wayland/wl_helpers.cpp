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

#include "util/log.hpp"

extern "C" {

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
