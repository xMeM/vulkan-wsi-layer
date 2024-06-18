/*
 * Copyright (c) 2019, 2021, 2024 Arm Limited.
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

#include "drm_utils.hpp"
#include "format_table.h"

namespace util
{
namespace drm
{

uint32_t vk_to_drm_format(VkFormat vk_format)
{
   for (size_t i = 0; i < fourcc_format_table_len; i++)
   {
      if (vk_format == fourcc_format_table[i].vk_format)
      {
         return fourcc_format_table[i].drm_format;
      }
   }

   for (size_t i = 0; i < srgb_fourcc_format_table_len; i++)
   {
      if (vk_format == srgb_fourcc_format_table[i].vk_format)
      {
         return srgb_fourcc_format_table[i].drm_format;
      }
   }

   return 0;
}

VkFormat drm_to_vk_format(uint32_t drm_format)
{
   for (size_t i = 0; i < fourcc_format_table_len; i++)
   {
      if (drm_format == fourcc_format_table[i].drm_format)
      {
         return fourcc_format_table[i].vk_format;
      }
   }

   return VK_FORMAT_UNDEFINED;
}

VkFormat drm_to_vk_srgb_format(uint32_t drm_format)
{
   for (size_t i = 0; i < srgb_fourcc_format_table_len; i++)
   {
      if (drm_format == srgb_fourcc_format_table[i].drm_format)
      {
         return srgb_fourcc_format_table[i].vk_format;
      }
   }

   return VK_FORMAT_UNDEFINED;
}

/* Returns the number of planes represented by a fourcc format. */
uint32_t drm_fourcc_format_get_num_planes(uint32_t format)
{
   switch (format)
   {
   default:
      return 0;

   case DRM_FORMAT_RGB332:
   case DRM_FORMAT_BGR233:
   case DRM_FORMAT_XRGB4444:
   case DRM_FORMAT_XBGR4444:
   case DRM_FORMAT_RGBX4444:
   case DRM_FORMAT_BGRX4444:
   case DRM_FORMAT_ARGB4444:
   case DRM_FORMAT_ABGR4444:
   case DRM_FORMAT_RGBA4444:
   case DRM_FORMAT_BGRA4444:
   case DRM_FORMAT_XRGB1555:
   case DRM_FORMAT_XBGR1555:
   case DRM_FORMAT_RGBX5551:
   case DRM_FORMAT_BGRX5551:
   case DRM_FORMAT_ARGB1555:
   case DRM_FORMAT_ABGR1555:
   case DRM_FORMAT_RGBA5551:
   case DRM_FORMAT_BGRA5551:
   case DRM_FORMAT_RGB565:
   case DRM_FORMAT_BGR565:
   case DRM_FORMAT_RGB888:
   case DRM_FORMAT_BGR888:
   case DRM_FORMAT_XRGB8888:
   case DRM_FORMAT_XBGR8888:
   case DRM_FORMAT_RGBX8888:
   case DRM_FORMAT_BGRX8888:
   case DRM_FORMAT_ARGB8888:
   case DRM_FORMAT_ABGR8888:
   case DRM_FORMAT_RGBA8888:
   case DRM_FORMAT_BGRA8888:
      return 1;
   }
}

} // namespace drm
} // namespace util
