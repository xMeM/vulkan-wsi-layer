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

/**
 * @file
 * @brief Vulkan WSI surface interfaces.
 */

#pragma once

#include <vulkan/vulkan.h>
#include "surface_properties.hpp"
#include "swapchain_base.hpp"

namespace wsi
{

/**
 * @brief Struct describing a DRM format with modifier.
 */
struct drm_format_pair
{
   uint32_t fourcc;
   uint64_t modifier;
};

/**
 * @brief A generic WSI representation of a VkSurface.
 *
 * The association between these objects and VkSurfaces is kept in the VkInstance private data.
 */
class surface
{
public:
   virtual ~surface() = default;

   /**
    * @brief Returns a @ref surface_properties implementation that can be specific to the VkSurface represented.
    */
   virtual surface_properties &get_properties() = 0;

   /**
    * @brief Allocates a swapchain for the VkSurface type represented.
    *
    * @param dev_data  The VkDevice associated private date.
    * @param allocator Allocation callbacks to use for host memory.
    *
    * @return nullptr on failure otherwise a constructed swapchain.
    */
   virtual util::unique_ptr<swapchain_base> allocate_swapchain(layer::device_private_data &dev_data,
                                                               const VkAllocationCallbacks *allocator) = 0;
};

} /* namespace wsi */
