/*
 * Copyright (c) 2017-2024 Arm Limited.
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
#include <stdbool.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <drm_fourcc.h>
#pragma GCC diagnostic pop

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
 *
 * Version History:
 * 1 - Initial wsialloc interface
 * 2 - Added WSIALLOC_ALLOCATE_HIGHEST_FIXED_RATE_COMPRESSION
 * 3 - Grouped the return values of wsialloc_alloc to wsialloc_allocate_result and added another value for returning
 *     whether or not the allocation will be disjoint.
 */
#define WSIALLOC_INTERFACE_VERSION 3

#define WSIALLOC_CONCAT(x, y) x##y
#define WSIALLOC_SYMBOL_VERSION(symbol, version) WSIALLOC_CONCAT(symbol, version)

/**
 * @brief This symbol must be defined by any implementation of the wsialloc interface as defined in this header.
 *
 * A wsialloc implementation defining this symbol is declaring it implements
 * the exact version of the wsialloc interface defined in this header.
 */
#define WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL \
   WSIALLOC_SYMBOL_VERSION(wsialloc_symbol_version_, WSIALLOC_INTERFACE_VERSION)
extern const uint32_t WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL;

/**
 * @brief Aborts if the wsialloc implementation version is not the expected one.
 *
 * This macro calls abort() if the version of the wsialloc implementation that was linked to the code using this
 * macro is not matching the version defined in the wsialloc.h header that is being included. This can be useful
 * to catch compatibility issues in code that links to an external static library implementing the wsialloc
 * interface.
 */
#define WSIALLOC_ASSERT_VERSION()                                            \
   if (WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL != WSIALLOC_INTERFACE_VERSION) \
   {                                                                         \
      abort();                                                               \
   }

/* Maximum number of planes that can be returned */
#define WSIALLOC_MAX_PLANES 4

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
   /** Sets a preference for selecting the format with the highest fixed compression rate. */
   WSIALLOC_ALLOCATE_HIGHEST_FIXED_RATE_COMPRESSION = 0x4,
};

typedef struct wsialloc_format
{
   uint32_t fourcc; /**< The DRM_FORMAT_<...> code */
   uint64_t modifier; /**< DRM modifier applied to all planes. */
   uint64_t flags; /**< Set of @r wsialloc_format_flag format flags. */
} wsialloc_format;

typedef struct wsialloc_allocate_info
{
   wsialloc_format *formats; /** List of formats to select from for the allocation. */
   unsigned format_count; /** Number of elements in formats array. */
   uint32_t width; /** The number of pixel columns required in the buffer. */
   uint32_t height; /** The number of pixel rows required in the buffer. */
   uint64_t flags; /** Set of @r wsialloc_allocate_flag allocation flags. */
} wsialloc_allocate_info;

typedef struct wsialloc_allocate_result
{
   wsialloc_format format; /** The selected format for allocation. */
   /** Per plane distance between rows of blocks divided by the block height measured in bytes. */
   int average_row_strides[WSIALLOC_MAX_PLANES];
   /** Per plane offset into the file descriptor for the start of the plane. */
   uint32_t offsets[WSIALLOC_MAX_PLANES];
   int buffer_fds[WSIALLOC_MAX_PLANES]; /** Per plane file descriptor for the buffer. */
   bool is_disjoint; /** Whether different fds will be used for each plane. */
} wsialloc_allocate_result;

/**
 * @brief Allocate a buffer from the WSI Allocator
 *
 * Allocate a buffer of size @p info::width x @p info::height in a way that is suitable for the underlying
 * window system and GPU.
 *
 * The allocation is made using a format from the list specified in @p info::formats . On success, @p result::format is set with
 * the selected format that was used for the allocation.
 *
 * Each plane is returned as a file descriptor. All other information returned about the buffer is also per-plane. It is
 * assumed the caller already knows how many planes are implied by the @p result::format that was selected.
 *
 * Each row in the buffer may be larger than @p info::width to account for buffer alignment requirements in the
 * underlying window system. @p result::average_row_strides must be examined to determine the number of bytes between
 * subsequent rows in each of the buffer's planes. Only positive average_row_strides are allowed.
 *
 * The client may free the buffer's planes by invoking close() on some or all of the elements of @p result::buffer_fds
 *
 * The same file descriptor ('fd') may be written to different elements of @p result::buffer_fds more than once, for some or all
 * of the planes. In this case:
 * - @p result::offsets @b must be used to determine where each plane starts in the file descriptor
 * - When the client frees the buffer, each unique fd in @p result::buffer_fds must only be closed once.
 *
 * Even if @p result::buffer_fds are all different or @p result::format is for a single plane, then the client must inspect
 * @p result::offsets in case it contains non-zero values.
 *
 * If @p result::buffer_fds are different then @p result::disjoint should be set to true.
 *
 * @note The implementation might not export the file descriptors in @p result::buffer_fds in such a way that allows the client
 * to directly map them on the CPU as writable (PROT_WRITE).
 *
 * The selected @p result::format modifier allows for a fourcc_mod_code() (as defined in drm_fourcc.h) to define
 * a reordering or other modification of the data in the buffer's planes (e.g. compression,
 * change in number of planes, etc).
 *
 * @p result::average_row_strides are per plane row average byte strides of the @p result::format. This is the number of bytes from the
 * start of a row of blocks divided by the block height. It may also have format specific meaning for formats not in those
 * categories.
 *
 * @p result::average_row_strides and @p result::offsets may have modifier-specific
 * meaning when a @p result::format with modifier is selected.
 *
 * @pre @p result::average_row_strides, @p result::buffer_fds, @p result::offsets
 * are pointers to storage large enough to hold per-plane information.
 * @pre @p info::width >=1 && @p info::height >= 1
 * @pre @p allocator is a currently valid WSI Allocator from wsialloc_new()
 * @post The allocated buffer will be zeroed.
 *
 * @param      allocator  The WSI Allocator to allocate from.
 * @param[in]  info       The requested allocation information.
 * @param[out] result     The allocation's result.

 * @retval WSIALLOC_ERROR_NONE          on successful buffer allocation.
 * @retval WSIALLOC_ERROR_INVALID       is returned for invalid parameters.
 * @retval WSIALLOC_ERROR_NOT_SUPPORTED is returned for unsupported parameters, such as a modifier or recognized
 *                                      format not supported by the underlying window-system/allocator.
 * @retval WSIALLOC_ERROR_NO_RESOURCE   is returned on failed buffer allocation due to lack of available memory or other
 *                                      system resources.
 */

wsialloc_error wsialloc_alloc(wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                              wsialloc_allocate_result *result);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WSIALLOC_H_ */
