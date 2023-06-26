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
 * @brief Implementation of a x11 WSI Surface
 */

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"

namespace wsi
{
namespace x11
{

struct surface::init_parameters
{
   const util::allocator &allocator;
   xcb_connection_t *connection;
   xcb_window_t window;
};

surface::surface(const init_parameters &params)
   : wsi::surface()
   , m_connection(params.connection)
   , m_window(params.window)
   , properties(this, params.allocator)
{
}

surface::~surface()
{
}

bool surface::init()
{
   auto dri3_cookie = xcb_dri3_query_version_unchecked(m_connection, 1, 2);
   auto dri3_reply = xcb_dri3_query_version_reply(m_connection, dri3_cookie, nullptr);
   auto has_dri3 = dri3_reply && (dri3_reply->major_version > 1 || dri3_reply->minor_version >= 2);
   free(dri3_reply);

   auto present_cookie = xcb_present_query_version_unchecked(m_connection, 1, 2);
   auto present_reply = xcb_present_query_version_reply(m_connection, present_cookie, nullptr);
   auto has_present = present_reply && (present_reply->major_version > 1 || present_reply->minor_version >= 2);
   free(present_reply);

   if (!has_dri3 || !has_present)
   {
      WSI_LOG_ERROR("DRI3 extension not present");
      return false;
   }

   return true;
}

bool surface::get_size_and_depth(uint32_t *width, uint32_t *height, int *depth)
{
   auto cookie = xcb_get_geometry(m_connection, m_window);
   if (auto *geom = xcb_get_geometry_reply(m_connection, cookie, nullptr))
   {
      *width = static_cast<uint32_t>(geom->width);
      *height = static_cast<uint32_t>(geom->height);
      *depth = static_cast<int>(geom->depth);
      free(geom);
      return true;
   }
   return false;
}

wsi::surface_properties &surface::get_properties()
{
   return properties;
}

util::unique_ptr<swapchain_base> surface::allocate_swapchain(layer::device_private_data &dev_data,
                                                             const VkAllocationCallbacks *allocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, allocator };
   auto chain = util::unique_ptr<swapchain_base>(alloc.make_unique<swapchain>(dev_data, allocator, this));

   return chain;
}

util::unique_ptr<surface> surface::make_surface(const util::allocator &allocator, xcb_connection_t *conn,
                                                xcb_window_t window)
{
   init_parameters params{ allocator, conn, window };
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

} /* namespace x11 */
} /* namespace wsi */
