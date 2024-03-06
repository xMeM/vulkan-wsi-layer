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
 * @brief Definitions for a headless WSI Surface
 */

#pragma once

#include "wsi/surface.hpp"
#include "surface_properties.hpp"
#include "drm_display.hpp"

namespace wsi
{
namespace display
{

class surface : public wsi::surface
{
public:
   surface(drm_display_mode *mode);
   wsi::surface_properties &get_properties() override;
   util::unique_ptr<swapchain_base> allocate_swapchain(layer::device_private_data &dev_data,
                                                       const VkAllocationCallbacks *allocator) override;

   VkExtent2D get_current_extent() const;
   void set_current_extent(VkExtent2D extent);
   drm_display_mode *get_display_mode();

private:
   drm_display_mode *m_display_mode;
   VkExtent2D m_current_extent;
   surface_properties m_surface_properties;
};

} /* namespace display */
} /* namespace wsi */
