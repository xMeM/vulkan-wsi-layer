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

#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"
#include <iostream>
#include <xcb/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/present.h>

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
   , connection(params.connection)
   , window(params.window)
   , properties(*this, params.allocator)
{
}

surface::~surface()
{
}

bool surface::getWindowSizeAndDepth(VkExtent2D *windowExtent, int *depth)
{
   auto cookie = xcb_get_geometry(connection, window);
   if (auto *geom = xcb_get_geometry_reply(connection, cookie, nullptr))
   {
      windowExtent->width = static_cast<uint32_t>(geom->width);
      windowExtent->height = static_cast<uint32_t>(geom->height);
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
      return wsi_surface;
   }
   return nullptr;
}

} /* namespace x11 */
} /* namespace wsi */
