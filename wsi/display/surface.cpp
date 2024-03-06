/*
 * Copyright (c) 2024 Arm Limited.
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
 * @brief Implementation of a headless WSI Surface
 */

#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"

namespace wsi
{
namespace display
{

surface::surface(drm_display_mode *display_mode)
   : m_display_mode(display_mode)
   , m_surface_properties(*this)
{
}

wsi::surface_properties &surface::get_properties()
{
   return m_surface_properties;
}

util::unique_ptr<swapchain_base> surface::allocate_swapchain(layer::device_private_data &dev_data,
                                                             const VkAllocationCallbacks *allocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, allocator };
   return util::unique_ptr<swapchain_base>(alloc.make_unique<swapchain>(dev_data, allocator, *this));
}

VkExtent2D surface::get_current_extent() const
{
   return m_current_extent;
}

void surface::set_current_extent(VkExtent2D extent)
{
   m_current_extent = extent;
}

drm_display_mode *surface::get_display_mode()
{
   return m_display_mode;
}

} /* namespace display */
} /* namespace wsi */
