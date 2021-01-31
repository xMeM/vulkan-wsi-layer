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

#include "format_table.h"

const fmt_spec fourcc_format_table[] = {
   /* Supported R,G,B,A formats */
   { DRM_FORMAT_RGB332, 1, { 8, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_BGR233, 1, { 8, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_XRGB4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_XBGR4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_RGBX4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_BGRX4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_ARGB4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_ABGR4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_RGBA4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_R4G4B4A4_UNORM_PACK16 },
   { DRM_FORMAT_BGRA4444, 1, { 16, 0, 0, 0 }, VK_FORMAT_B4G4R4A4_UNORM_PACK16 },
   { DRM_FORMAT_XRGB1555, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_XBGR1555, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_RGBX5551, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_BGRX5551, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_ARGB1555, 1, { 16, 0, 0, 0 }, VK_FORMAT_A1R5G5B5_UNORM_PACK16 },
   { DRM_FORMAT_ABGR1555, 1, { 16, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_RGBA5551, 1, { 16, 0, 0, 0 }, VK_FORMAT_R5G5B5A1_UNORM_PACK16 },
   { DRM_FORMAT_BGRA5551, 1, { 16, 0, 0, 0 }, VK_FORMAT_B5G5R5A1_UNORM_PACK16 },
   { DRM_FORMAT_RGB565, 1, { 16, 0, 0, 0 }, VK_FORMAT_R5G6B5_UNORM_PACK16 },
   { DRM_FORMAT_BGR565, 1, { 16, 0, 0, 0 }, VK_FORMAT_B5G6R5_UNORM_PACK16 },
   { DRM_FORMAT_RGB888, 1, { 24, 0, 0, 0 }, VK_FORMAT_B8G8R8_UNORM },
   { DRM_FORMAT_BGR888, 1, { 24, 0, 0, 0 }, VK_FORMAT_R8G8B8_UNORM },
   { DRM_FORMAT_XRGB8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_XBGR8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_RGBX8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_BGRX8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_ARGB8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_B8G8R8A8_UNORM },
   { DRM_FORMAT_ABGR8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_R8G8B8A8_UNORM },
   { DRM_FORMAT_RGBA8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
   { DRM_FORMAT_BGRA8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_UNDEFINED },
};

const fmt_spec srgb_fourcc_format_table[] = {
   { DRM_FORMAT_ARGB8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_B8G8R8A8_SRGB },
   { DRM_FORMAT_ABGR8888, 1, { 32, 0, 0, 0 }, VK_FORMAT_R8G8B8A8_SRGB },
};

const size_t fourcc_format_table_len = NELEMS(fourcc_format_table);
const size_t srgb_fourcc_format_table_len = NELEMS(srgb_fourcc_format_table);
