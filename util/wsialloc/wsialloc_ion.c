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
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief Version of the wsialloc interface we are implementing in this file.
 *
 * This should only be increased when this implementation is updated to match newer versions of wsialloc.h.
 */
#define WSIALLOC_IMPLEMENTATION_VERSION 1

/* Ensure we are implementing the wsialloc version matching the wsialloc.h header we are using. */
#if WSIALLOC_IMPLEMENTATION_VERSION != WSIALLOC_INTERFACE_VERSION
#error "Version mismatch between wsialloc implementation and interface version"
#endif

const uint32_t WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL = WSIALLOC_IMPLEMENTATION_VERSION;

/** Default alignment */
#define WSIALLOCP_MIN_ALIGN_SZ (64u)
/** Maximum image size allowed for each dimension */
#define MAX_IMAGE_SIZE 128000

struct wsialloc_allocator
{
   /* File descriptor of /dev/ion. */
   int fd;
   /* Allocator heap id. */
   uint32_t alloc_heap_id;
   /* Protected allocator heap id */
   uint32_t protected_alloc_heap_id;
   bool protected_heap_exists;
};

typedef struct wsialloc_format_descriptor
{
   wsialloc_format format;
   fmt_spec format_spec;
} wsialloc_format_descriptor;

static int find_alloc_heap_id(int fd)
{
   assert(fd != -1);

   struct ion_heap_data heaps[ION_NUM_HEAP_IDS];
   struct ion_heap_query query = {
      .cnt = ION_NUM_HEAP_IDS, .heaps = (uint64_t)(uintptr_t)heaps,
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

static int allocate(int fd, size_t size, uint32_t heap_id)
{
   assert(size > 0);
   assert(fd != -1);

   struct ion_allocation_data alloc = {
      .len = size, .heap_id_mask = 1u << heap_id, .flags = 0,
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

wsialloc_error wsialloc_new(wsialloc_allocator **allocator)
{
   assert(allocator != NULL);
   wsialloc_error ret = WSIALLOC_ERROR_NONE;

   wsialloc_allocator *ion = malloc(sizeof(wsialloc_allocator));
   if (NULL == ion)
   {
      ret = WSIALLOC_ERROR_NO_RESOURCE;
      goto fail;
   }

   ion->fd = open("/dev/ion", O_RDONLY);
   if (ion->fd < 0)
   {
      ret = WSIALLOC_ERROR_NO_RESOURCE;
      goto fail;
   }

   ion->alloc_heap_id = find_alloc_heap_id(ion->fd);
   if (ion->alloc_heap_id < 0)
   {
      ret = WSIALLOC_ERROR_NO_RESOURCE;
      goto fail;
   }

   ion->protected_heap_exists = false;
   *allocator = ion;
   return ret;
fail:
   wsialloc_delete(ion);
   return ret;
}

void wsialloc_delete(wsialloc_allocator *allocator)
{
   if (NULL == allocator)
   {
      return;
   }

   if (allocator->fd >= 0)
   {
      close(allocator->fd);
   }

   free(allocator);
}

static wsialloc_error calculate_format_properties(const wsialloc_format_descriptor *descriptor,
                                                  const wsialloc_allocate_info *info, int *strides, uint32_t *offsets)
{
   assert(descriptor != NULL);
   assert(info != NULL);
   assert(strides != NULL);
   assert(offsets != NULL);

   const uint8_t *bits_per_pixel = descriptor->format_spec.bpp;
   const uint64_t flags = descriptor->format.flags;
   const uint64_t modifier = descriptor->format.modifier;
   const uint32_t num_planes = descriptor->format_spec.nr_planes;

   /* We currently don't support any kind of custom modifiers */
   if (modifier != DRM_FORMAT_MOD_LINEAR)
   {
      return WSIALLOC_ERROR_NOT_SUPPORTED;
   }
   /* No multi-plane format support */
   if (num_planes > 1)
   {
      return WSIALLOC_ERROR_NOT_SUPPORTED;
   }

   size_t size = 0;
   for (size_t plane = 0; plane < num_planes; plane++)
   {
      /* Assumes multiple of 8--rework otherwise. */
      const uint32_t plane_bytes_per_pixel = bits_per_pixel[plane] / 8;
      assert(plane_bytes_per_pixel * 8 == bits_per_pixel[plane]);

      /* With large enough width, this can overflow as strides are signed. In practice, this shouldn't happen */
      strides[plane] = round_size_up_to_align(info->width * plane_bytes_per_pixel);

      offsets[plane] = size;

      size += strides[plane] * info->height;
   }

   return WSIALLOC_ERROR_NONE;
}

static wsialloc_error allocate_format(const wsialloc_allocator *allocator, const wsialloc_format_descriptor *descriptor,
                                      const wsialloc_allocate_info *info, const int *strides, const uint32_t *offsets,
                                      int *buffer_fds)
{
   assert(allocator != NULL);
   assert(descriptor != NULL);
   assert(info != NULL);
   assert(offsets != NULL);
   assert(strides != NULL);
   assert(strides[0] >= 0);
   assert(buffer_fds != NULL);

   const uint64_t flags = descriptor->format.flags;
   const uint32_t num_planes = descriptor->format_spec.nr_planes;

   /* The only error that can be encountered on allocations is lack of resources. Other parameter validation and
    * support checks are done on format selection. */
   assert(num_planes == 1);
   uint32_t alloc_heap_id = allocator->alloc_heap_id;
   if (info->flags & WSIALLOC_ALLOCATE_PROTECTED)
   {
      /* Exit if we don't support allocating protected memory */
      if (!allocator->protected_heap_exists)
      {
         return WSIALLOC_ERROR_NO_RESOURCE;
      }
      alloc_heap_id = allocator->protected_alloc_heap_id;
   }

   uint64_t total_size = offsets[0] + (uint64_t)strides[0] * info->height;
   if (total_size > SIZE_MAX)
   {
      return WSIALLOC_ERROR_NO_RESOURCE;
   }
   buffer_fds[0] = allocate(allocator->fd, (size_t)total_size, alloc_heap_id);

   if (buffer_fds[0] < 0)
   {
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   return WSIALLOC_ERROR_NONE;
}

static const fmt_spec *find_format(uint32_t fourcc)
{
   /* Mask off any bits not necessary for allocation size */
   fourcc = fourcc & (~(uint32_t)DRM_FORMAT_BIG_ENDIAN);

   /* Search table for the format*/
   for (size_t i = 0; i < fourcc_format_table_len; i++)
   {
      if (fourcc == fourcc_format_table[i].drm_format)
      {
         const fmt_spec *found_fmt = &fourcc_format_table[i];
         assert(found_fmt->nr_planes <= WSIALLOC_MAX_PLANES);

         return found_fmt;
      }
   }

   return NULL;
}

static bool validate_parameters(const wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                                const wsialloc_format *format, const int *strides, const uint32_t *offsets)
{
   if (allocator == NULL)
   {
      return false;
   }
   else if (!strides || !offsets)
   {
      return false;
   }
   else if (info->format_count == 0 || info->formats == NULL)
   {
      return false;
   }
   else if (info->width < 1 || info->height < 1 || info->width > MAX_IMAGE_SIZE || info->height > MAX_IMAGE_SIZE)
   {
      return false;
   }

   return true;
}

wsialloc_error wsialloc_alloc(wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                              wsialloc_format *format, int *strides, int *buffer_fds, uint32_t *offsets)
{
   assert(allocator != NULL);
   assert(info != NULL);
   assert(format != NULL);
   assert(strides != NULL);
   assert(offsets != NULL);

   if (!validate_parameters(allocator, info, format, strides, offsets))
   {
      return WSIALLOC_ERROR_INVALID;
   }

   int local_strides[WSIALLOC_MAX_PLANES];
   int local_fds[WSIALLOC_MAX_PLANES] = { -1 };
   int local_offsets[WSIALLOC_MAX_PLANES];
   wsialloc_error err = WSIALLOC_ERROR_NONE;
   wsialloc_format_descriptor selected_format_desc = {};

   for (size_t i = 0; i < info->format_count; i++)
   {
      const wsialloc_format *current_format = &info->formats[i];
      const fmt_spec *format_spec = find_format(current_format->fourcc);
      if (!format_spec)
      {
         err = WSIALLOC_ERROR_NOT_SUPPORTED;
         continue;
      }

      wsialloc_format_descriptor current_format_desc = { *current_format, *format_spec };
      err = calculate_format_properties(&current_format_desc, info, local_strides, local_offsets);
      if (err != WSIALLOC_ERROR_NONE)
      {
         continue;
      }

      /* A compatible format was found */
      selected_format_desc = current_format_desc;
      break;
   }

   if (err == WSIALLOC_ERROR_NONE)
   {
      if (!(info->flags & WSIALLOC_ALLOCATE_NO_MEMORY))
      {
         err = allocate_format(allocator, &selected_format_desc, info, local_strides, local_offsets, local_fds);
      }
   }

   if (err == WSIALLOC_ERROR_NONE)
   {
      *format = selected_format_desc.format;
      *strides = local_strides[0];
      *offsets = local_offsets[0];
      if (!(info->flags & WSIALLOC_ALLOCATE_NO_MEMORY))
      {
         *buffer_fds = local_fds[0];
      }
   }
   return err;
}
