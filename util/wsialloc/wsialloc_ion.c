/*
 * Copyright (c) 2017, 2019, 2021 Arm Limited.
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

#include "wsialloc.h"
#include "format_table.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ion.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define UNUSED(x) ((void)(x))

/** Default alignment */
#define WSIALLOCP_MIN_ALIGN_SZ (64u)

struct wsialloc_allocator
{
   /* File descriptor of /dev/ion. */
   int fd;
   /* Allocator heap id. */
   uint32_t alloc_heap_id;
};

static int find_alloc_heap_id(int fd)
{
   assert(fd != -1);

   struct ion_heap_data heaps[ION_NUM_HEAP_IDS];
   struct ion_heap_query query = {
      .cnt = ION_NUM_HEAP_IDS,
      .heaps = (uint64_t)(uintptr_t)heaps,
   };

   int ret = ioctl(fd, ION_IOC_HEAP_QUERY, &query);
   if (ret < 0)
   {
      return ret;
   }

   int alloc_heap_id = -1;
   for (uint32_t i = 0; i < query.cnt; ++i)
   {
      if (ION_HEAP_TYPE_DMA == heaps[i].type)
      {
         alloc_heap_id = heaps[i].heap_id;
         break;
      }
   }

   return alloc_heap_id;
}

static int allocate(int fd, uint64_t size, uint32_t heap_id)
{
   assert(size > 0);
   assert(fd != -1);

   struct ion_allocation_data alloc = {
      .len = size,
      .heap_id_mask = 1u << heap_id,
      .flags = 0,
   };
   int ret = ioctl(fd, ION_IOC_ALLOC, &alloc);
   if (ret < 0)
   {
      return ret;
   }

   return alloc.fd;
}

static uint64_t round_size_up_to_align(uint64_t size)
{
   return (size + WSIALLOCP_MIN_ALIGN_SZ - 1) & ~(WSIALLOCP_MIN_ALIGN_SZ - 1);
}

wsialloc_allocator *wsialloc_new(int external_fd)
{
   UNUSED(external_fd);

   wsialloc_allocator *ion = NULL;
   ion = malloc(sizeof(*ion));
   if (NULL == ion)
   {
      goto fail;
   }

   ion->fd = open("/dev/ion", O_RDONLY);
   if (ion->fd < 0)
   {
      goto fail;
   }

   ion->alloc_heap_id = find_alloc_heap_id(ion->fd);
   if (ion->alloc_heap_id < 0)
   {
      goto fail;
   }

   return ion;
fail:
   wsialloc_delete(ion);
   return NULL;
}

int wsialloc_delete(wsialloc_allocator *ion)
{
   int ret = 0;

   if (NULL == ion)
   {
      return 0;
   }

   if (ion->fd != -1)
   {
      if (close(ion->fd) != 0)
      {
         ret = -errno;
      }
   }

   free(ion);
   return ret;
}

static int wsiallocp_get_fmt_info(uint32_t fourcc_fmt, uint32_t *nr_planes, uint32_t *plane_bpp)
{
   unsigned int fmt_idx;
   const fmt_spec *found_fmt;
   unsigned int plane_idx;

   assert(nr_planes != NULL && plane_bpp != NULL);

   /* Mask off any bits not necessary for allocation size */
   fourcc_fmt = fourcc_fmt & (~(uint32_t)DRM_FORMAT_BIG_ENDIAN);

   /* Search table for the format*/
   for (fmt_idx = 0; fmt_idx < fourcc_format_table_len; ++fmt_idx)
   {
      if (fourcc_fmt == fourcc_format_table[fmt_idx].drm_format)
      {
         break;
      }
   }

   if (fmt_idx >= fourcc_format_table_len)
   {
      return -ENOTSUP;
   }

   /* fmt_idx is now a correct index into the table */
   found_fmt = &fourcc_format_table[fmt_idx];

   assert(found_fmt->nr_planes <= WSIALLOCP_MAX_PLANES);
   *nr_planes = found_fmt->nr_planes;

   /* Only write out as many bpp as there are planes */
   for (plane_idx = 0; plane_idx < found_fmt->nr_planes; ++plane_idx)
   {
      plane_bpp[plane_idx] = (uint32_t)found_fmt->bpp[plane_idx];
   }

   return 0;
}

int wsialloc_alloc(
    wsialloc_allocator *ion,
    uint32_t fourcc,
    uint32_t width,
    uint32_t height,
    int *stride,
    int *new_fd,
    uint32_t *offset,
    const uint64_t *modifier)
{
   assert(ion != NULL);
   assert(fourcc != 0);
   assert(width > 0);
   assert(height > 0);
   assert(stride != NULL);
   assert(new_fd != NULL);
   assert(offset != NULL);

   if (modifier != NULL && *modifier != 0)
   {
      return -ENOTSUP;
   }

   /* Validate format and determine per-plane bits per pixel. */
   uint32_t nr_planes, bits_per_pixel[WSIALLOCP_MAX_PLANES];
   int ret = wsiallocp_get_fmt_info(fourcc, &nr_planes, bits_per_pixel);
   if (ret != 0)
   {
      return ret;
   }

   size_t size = 0;
   for (uint32_t plane = 0; plane < nr_planes; plane++)
   {
      offset[plane] = size;

      /* Assumes multiple of 8--rework otherwise. */
      const uint32_t plane_bytes_per_pixel = bits_per_pixel[plane] / 8;
      assert(plane_bytes_per_pixel * 8 == bits_per_pixel[plane]);

      stride[plane] = round_size_up_to_align(width * plane_bytes_per_pixel);
      size += stride[plane] * height;
   }

   new_fd[0] = allocate(ion->fd, size, ion->alloc_heap_id);
   if (new_fd[0] < 0)
   {
      return -errno;
   }

   for (uint32_t plane = 1; plane < nr_planes; plane++)
   {
      new_fd[plane] = new_fd[0];
   }

   return 0;
}
