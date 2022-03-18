/*
 * Copyright (c) 2022 Arm Limited.
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

#include <vulkan/vulkan.h>
#include "custom_allocator.hpp"

namespace util
{

/**
 * @brief Get the properties a format has when combined with a DRM modifier.
 *
 * @param      physical_device   The physical device
 * @param      format            The target format.
 * @param[out] format_props_list A vector which will store the supported properties
 *                               for every modifier.
 *
 * @return VK_SUCCESS on success. VK_ERROR_OUT_OF_HOST_MEMORY is returned when
 * the host gets out of memory.
 */
VkResult get_drm_format_properties(VkPhysicalDevice physical_device, VkFormat format,
                                   util::vector<VkDrmFormatModifierPropertiesEXT> &format_props_list);

} /* namespace util */
