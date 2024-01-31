/*
 * Copyright (c) 2021, 2023-2024 Arm Limited.
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
 * @file macros.hpp
 *
 * @brief Contains useful utility macros used across the project.
 */
#include <vulkan/vulkan.h>

/*
 * Macros that are used to mark functions signatures.
 *
 *  VWL_VKAPI_CALL        - Replaces the return type of the function. Use to mark functions to use the expected
 *                          Vulkan calling conventions.
 *  VWL_CAPI_CALL         - Replaces the return type of the function. Use to mark other non-Vulkan functions
 *                          that should use the C calling convention, such as callbacks implemented in C++ that
 *                          are used by C code.
 *  VWL_API_POST          - Placed at the end of the function signature. These will typically be
 *                          functions that need to be callable from C.
 *  VWL_VKAPI_EXPORT      - Marks that the symbol should use the "default" visibility
 */
#define VWL_VKAPI_CALL(ret_type) extern "C" VKAPI_ATTR ret_type VKAPI_CALL
#define VWL_CAPI_CALL(ret_type) extern "C" ret_type
#define VWL_API_POST noexcept

#if defined(__GNUC__) && __GNUC__ >= 4
#define VWL_VKAPI_EXPORT __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define VWL_VKAPI_EXPORT __attribute__((visibility("default")))
#else
#define VWL_VKAPI_EXPORT
#endif

/* Unused parameter macro */
#define UNUSED(x) ((void)(x))