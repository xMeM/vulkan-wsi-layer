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
 * The client may have already obtained a file descriptor (fd) for a Direct Rendering Manager (DRM) driver so it may
 * perform other operations (e.g. presenting a buffer). Hence, such an fd is passed in on Allocator Construction so that
 * the underlying allocator may allocate from the existing fd. However, the underlying implementation is also free to
 * ignore that fd and use some other allocation mechanism, even in a production system.
 *
 * The underlying allocator implementation is chosen at compile time.
 *
 * All API Public entry points are implemented in a way that is thread safe, and the client does not need to be
 * concerned with locking when using these APIs. If implementers make use of non-threadsafe functions, writable global
 * data etc they must make appropriate use of locks.
 */

/**
 * @brief Union for allocator type.
 *
 * Allocators are usually file descriptors of the allocating device. However,
 * this interface provides the possibility to define private allocator structures.
 */
typedef union wsialloc_allocator
{
   void *ptr;
   intptr_t fd;
} wsialloc_allocator;

/**
 * @brief Allocate and initialize a new WSI Allocator from an existing file descriptor.
 *
 * The allocator from the function is used in subsequent calls to wsialloc_alloc() to allocate new buffers.
 *
 * This function will be implemented as thread-safe. The implementer must ensure they use thread-safe functions, or use
 * appropriate locking for non-threadsafe functions/writable global data.
 *
 * @note The underlying implementation may choose to use @p external_fd or its own platform-specific allocation method.
 *
 * @param      external_fd file descriptor that the WSI Allocator could use for allocating new buffers
 * @param[out] allocator   a valid allocator for use in wsialloc functions
 * @retval     0           indicates success in creating the allocator
 * @retval     non-zero    indicates an error
 */
int wsialloc_new(int external_fd, wsialloc_allocator *allocator);

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
 * This function will be implemented as thread-safe. The implementer must ensure they use thread-safe functions, or use
 * appropriate locking for non-threadsafe functions/writable global data.
 *
 * @post no more new allocations should be made on @p allocator even if previous allocations still exist.
 *
 * @param      allocator  The allocator to close down and free
 * @retval     0          on success
 * @retval     non-zero   on failure with errno
 */
int wsialloc_delete(wsialloc_allocator *allocator);

/**
 * @brief Allocate a buffer from the WSI Allocator
 *
 * Allocate a buffer of size @p width x @p height of format @p fourcc_fmt in a way that is suitable for the underlying
 * window system and GPU.
 *
 * Each plane is returned as a file descriptor. All other information returned about the buffer is also per-plane. It is
 * assumed the caller already knows how many planes are implied by @p fourcc_fmt and @p modifiers.
 *
 * Each row in the buffer may be larger than @p width to account for buffer alignment requirements in the underlying
 * window system. @p strides must be examined to determine the number of bytes between subsequent rows in each of the
 * buffer's planes. Only positive strides are allowed.
 *
 * The client may free the buffer's planes by invoking close() on some or all of the elements of @p buffer_fds
 *
 * The same file descriptor ('fd') may be written to different elements of @p buffer_fds more than once, for some or all
 * of the planes. In this case:
 * - @p offsets @b must be used to determine where each plane starts in the file descriptor
 * - When the client frees the buffer, each unique fd in @p buffer_fds must only be closed once
 *
 * Even if @p buffer_fds are all different or @p fourcc_fmt is for a single plane, then the client must inspect @p
 * offsets in case it contains non-zero values.
 *
 * @note The implementation might not export the file descriptors in @p buffer_fds in such a way that allows the client
 * to directly map them on the CPU as writable (PROT_WRITE).
 *
 * @p modifiers allows for a fourcc_mod_code() (as defined in drm_fourcc.h) to be passed in
 * per-plane to request that allocation account for a reordering or other modification of the data in the buffer's
 * planes (e.g. compression, change in number of planes, etc).
 *
 * @p strides and @p offsets may have modifier-specific meaning when @p modifiers are in use.
 *
 * This function will be implemented as thread-safe. The implementer must ensure they use thread-safe functions, or use
 * appropriate locking for non-threadsafe functions/writable global data.
 *
 * @pre @p strides, @p buffer_fds, @p offsets are pointers to storage large enough to hold per-plane information
 * @pre @p width >=1 && @p height >= 1
 * @pre @p allocator is a currently valid WSI Allocator from wsialloc_new()
 * @post The allocated buffer will be zeroed.
 *
 * @param      allocator  The WSI Allocator to allocate from
 * @param      fourcc_fmt The DRM_FORMAT_<...> code
 * @param      width      The number of pixel columns required in the buffer
 * @param      height     The number of pixel rows required in the buffer
 * @param[out] strides    Per-plane number of bytes between successive rows in the buffer.
 * @param[out] buffer_fds Per-plane file descriptors for the buffer
 * @param[out] offsets    Per-plane offset into the file descriptor for the start of the plane
 * @param[in]  modifiers  Per-plane modifiers or NULL if no modifiers required
 * @retval     0          on successful buffer allocation
 * @retval     Non-zero   indicates some error listed below and/or a window-system/allocator specific (usually negative)
 *                        error
 * @retval     -EINVAL    is also returned for invalid parameters
 * @retval     -ENOTSUP   is also returned for unsupported parameters, such as a modifier or recognized format
 *                        not supported by the underlying window-system/allocator
 */
int wsialloc_alloc(wsialloc_allocator *allocator, uint32_t fourcc_fmt, uint32_t width, uint32_t height, int *strides, int *buffer_fds,
                   uint32_t *offsets, const uint64_t *modifiers);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WSIALLOC_H_ */
