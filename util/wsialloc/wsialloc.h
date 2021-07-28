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

/**
 * @file
 * Window System Integration (WSI) Buffer Allocation Interface
 */

#ifndef _WSIALLOC_H_
#define _WSIALLOC_H_

#include <stdint.h>
#include <drm_fourcc.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @page wsialloc_page Window System Integration (WSI) Buffer Allocation
 *
 * wsialloc is a standalone module for doing window system/platform agnostic multi-plane buffer allocation.
 *
 * The underlying implementation will allocate sufficient space for the desired buffer format in a way that is
 * compatible with the window system and the GPU (e.g. accounting for buffer row-start alignment requirements).
 *
 * @note The bare minimum work is done to provide the buffer. For example, it is up to the client to initialize the data
 * if that is required for the desired buffer format.
 *
 * The underlying allocator implementation is chosen at compile time.
 *
 * All API Public entry points are implemented in a way that is thread safe, and the client does not need to be
 * concerned with locking when using these APIs. If implementers make use of non-threadsafe functions, writable global
 * data etc they must make appropriate use of locks.
 */

/**
 * @brief Allocator type.
 *
 * Represents a private allocator structure.
 */
typedef struct wsialloc_allocator wsialloc_allocator;

typedef enum wsialloc_error
{
   WSIALLOC_ERROR_NONE = 0,
   WSIALLOC_ERROR_INVALID = -1,
   WSIALLOC_ERROR_NOT_SUPPORTED = -2,
   WSIALLOC_ERROR_NO_RESOURCE = -3,
} wsialloc_error;

/**
 * @brief Allocate and initialize a new WSI Allocator from an existing file descriptor.
 *
 * The allocator from the function is used in subsequent calls to wsialloc_alloc() to allocate new buffers.
 *
 * @param[out] allocator a valid allocator for use in wsialloc functions.
 *
 * @retval WSIALLOC_ERROR_NONE          on successful allocator creation.
 * @retval WSIALLOC_ERROR_FAILED        on failed allocator creation.
 */
wsialloc_error wsialloc_new(wsialloc_allocator **allocator);

/**
 * @brief Close down and free resources associated with a WSI Allocator
 *
 * It is acceptable for buffer allocations from @p allocator to still exist and be in use. In this case, this function
 * will return without waiting for the client to free the other allocations. However, the actual closing down and
 * freeing of @p allocator will be deferred until the last allocation has been closed by the client.
 *
 * @note For the implementer: there is usually no need to track this explicitly, since kernel side allocators
 * automatically defer their closing until all their allocations are also freed.
 *
 * @post no more new allocations should be made on @p allocator even if previous allocations still exist.
 *
 * @param allocator  The allocator to close down and free.
 */
void wsialloc_delete(wsialloc_allocator *allocator);

enum wsialloc_format_flag
{
   /** The format requires a memory allocation with the same file descriptor for all planes. */
   WSIALLOC_FORMAT_NON_DISJOINT = 0x1,
};

enum wsialloc_allocate_flag
{
   /** Allocates the buffer in protected memory. */
   WSIALLOC_ALLOCATE_PROTECTED = 0x1,
   /** Performs allocation calculations and format selection without allocating any memory. */
   WSIALLOC_ALLOCATE_NO_MEMORY = 0x2,
};

typedef struct wsialloc_format
{
   uint32_t fourcc;   /**< The DRM_FORMAT_<...> code */
   uint64_t modifier; /**< DRM modifier applied to all planes. */
   uint64_t flags;    /**< Set of @r wsialloc_format_flag format flags. */
} wsialloc_format;

typedef struct wsialloc_allocate_info
{
   wsialloc_format *formats; /** List of formats to select from for the allocation */
   unsigned format_count;    /** Number of elements in formats array */
   uint32_t width;           /** The number of pixel columns required in the buffer. */
   uint32_t height;          /** The number of pixel rows required in the buffer. */
   uint64_t flags;           /** Set of @r wsialloc_allocate_flag allocation flags. */
} wsialloc_allocate_info;

/**
 * @brief Allocate a buffer from the WSI Allocator
 *
 * Allocate a buffer of size @p info::width x @p info::height in a way that is suitable for the underlying
 * window system and GPU.
 *
 * The allocation is made using a format from the list specified in @p info::formats . On success, @p format is set with
 * the selected format that was used for the allocation.
 *
 * Each plane is returned as a file descriptor. All other information returned about the buffer is also per-plane. It is
 * assumed the caller already knows how many planes are implied by the @p format that was selected.
 *
 * Each row in the buffer may be larger than @p info::width to account for buffer alignment requirements in the
 * underlying window system. @p strides must be examined to determine the number of bytes between subsequent rows
 * in each of the buffer's planes. Only positive strides are allowed.
 *
 * The client may free the buffer's planes by invoking close() on some or all of the elements of @p buffer_fds
 *
 * The same file descriptor ('fd') may be written to different elements of @p buffer_fds more than once, for some or all
 * of the planes. In this case:
 * - @p offsets @b must be used to determine where each plane starts in the file descriptor
 * - When the client frees the buffer, each unique fd in @p buffer_fds must only be closed once.
 *
 * Even if @p buffer_fds are all different or @p format is for a single plane, then the client must inspect @p offsets
 * in case it contains non-zero values.
 *
 * @note The implementation might not export the file descriptors in @p buffer_fds in such a way that allows the client
 * to directly map them on the CPU as writable (PROT_WRITE).
 *
 * The selected @p format modifier allows for a fourcc_mod_code() (as defined in drm_fourcc.h) to define
 * a reordering or other modification of the data in the buffer's planes (e.g. compression,
 * change in number of planes, etc).
 *
 * @p strides is the per plane row byte stride of the @p format. For linear formats this is the number of bytes from the
 * start of a row to the start of the next row. For block based formats it is the number of bytes from the start of one
 * row of blocks to the start of the next. It may also have format specific meaning for formats not in those categories.
 *
 * @p strides and @p offsets may have modifier-specific meaning when a @p format with modifier is selected.
 *
 * @pre @p strides, @p buffer_fds, @p offsets are pointers to storage large enough to hold per-plane information
 * @pre @p info::width >=1 && @p info::height >= 1
 * @pre @p allocator is a currently valid WSI Allocator from wsialloc_new()
 * @post The allocated buffer will be zeroed.
 *
 * @param      allocator  The WSI Allocator to allocate from.
 * @param[in]  info       The requested allocation information.
 * @param[out] format     The selected format for allocation.
 * @param[out] strides    Per-plane row byte stride of the buffer.
 * @param[out] buffer_fds Per-plane file descriptors for the buffer.
 * @param[out] offsets    Per-plane offset into the file descriptor for the start of the plane.

 * @retval WSIALLOC_ERROR_NONE          on successful buffer allocation.
 * @retval WSIALLOC_ERROR_INVALID       is returned for invalid parameters.
 * @retval WSIALLOC_ERROR_NOT_SUPPORTED is returned for unsupported parameters, such as a modifier or recognized
 *                                      format not supported by the underlying window-system/allocator.
 * @retval WSIALLOC_ERROR_NO_RESOURCE   is returned on failed buffer allocation due to lack of available memory or other
 *                                      system resources.
 */

wsialloc_error wsialloc_alloc(wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                              wsialloc_format *format, int *strides, int *buffer_fds, uint32_t *offsets);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WSIALLOC_H_ */
