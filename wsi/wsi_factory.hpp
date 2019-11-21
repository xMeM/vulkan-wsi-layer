
/*
 * Copyright (c) 2019 Arm Limited.
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
 * @brief Contains the factory methods for obtaining the specific surface and swapchain implementations.
 */
#pragma once

#include "swapchain_base.hpp"
#include "surface_properties.hpp"
#include <layer/private_data.hpp>

namespace wsi
{

/**
 * Obtains the surface properties for the specific surface type.
 *
 * @param surface The surface for which to get the properties.
 *
 * @return nullptr if surface type is unsupported.
 */
surface_properties *get_surface_properties(VkSurfaceKHR surface);

/**
 * Allocates a surface specific swapchain.
 *
 * @param surface    The surface for which a swapchain is allocated.
 * @param dev_data   The device specific data.
 * @param pAllocator The allocator from which to allocate any memory.
 *
 * @return nullptr on failure.
 */
swapchain_base *allocate_surface_swapchain(VkSurfaceKHR surface, layer::device_private_data &dev_data,
                                           const VkAllocationCallbacks *pAllocator);

/**
 * Destroys a swapchain and frees memory. Used with @ref allocate_surface_swapchain.
 *
 * @param swapchain  Pointer to the swapchain to destroy.
 * @param pAllocator The allocator to use for freeing memory.
 */
void destroy_surface_swapchain(swapchain_base *swapchain, const VkAllocationCallbacks *pAllocator);

} // namespace wsi