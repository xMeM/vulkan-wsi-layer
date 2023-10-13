/*
 * Copyright (c) 2021-2023 Arm Limited.
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
namespace util
{
#define WSI_DEFAULT_LOG_LEVEL 1

/**
 * @brief Log a message to a certain log level
 *
 * @details For the log level, we use a bigger integer to represent an increased
 * level of verbosity. If this is not specified, the log level is default to 1.
 * We use a "staircase" approach with respect to printing logs. We  print all log
 * messages equal or below the log level set, e.g. if VULKAN_WSI_DEBUG_LEVEL
 * is set to 2, messages with log level 1 and 2 are printed. Please note that
 * the newline character '\n' is automatically appended.
 *
 * @param[in] level     The log level of this message, you can set an arbitary
 *                      integer however please refer to the included macros for
 *                      the sensible defaults.
 * @param[in] file      The source file name (``__FILE__``)
 * @param[in] line      The source file line number (``__LINE__``)
 * @param[in] format    A C-style formatting string.
 */

void wsi_log_message(int level, const char *file, int line, const char *format, ...)
#ifdef __GNUC__
   __attribute__((format(printf, 4, 5)))
#endif
   ;

#ifdef NDEBUG
static constexpr bool wsi_log_enable = false;
#else
static constexpr bool wsi_log_enable = true;
#endif

#define WSI_LOG(level, ...)                                               \
   do                                                                     \
   {                                                                      \
      if (::util::wsi_log_enable)                                         \
         ::util::wsi_log_message(level, __FILE__, __LINE__, __VA_ARGS__); \
   } while (0)

#define WSI_LOG_ERROR(...) WSI_LOG(1, __VA_ARGS__)
#define WSI_LOG_WARNING(...) WSI_LOG(2, __VA_ARGS__)
#define WSI_LOG_INFO(...) WSI_LOG(3, __VA_ARGS__)

} /* namespace util */
