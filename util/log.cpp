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

#include "log.hpp"
#include <iostream>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>

namespace util
{

#ifndef NDEBUG

/**
 * @brief check if a log level is enabled, and print it
 */
static bool check_and_print_log_level(int level)
{
   struct log_state
   {
      int level = WSI_DEFAULT_LOG_LEVEL;
      log_state()
      {
         if (const char *env = std::getenv("VULKAN_WSI_DEBUG_LEVEL"))
         {
            std::from_chars(env, env + std::strlen(env), level);
         }
      }
   };
   static log_state state;

   bool result = level <= state.level;
   if (result)
   {
      switch (level)
      {
      case 0:
         /* Reserved for no logging */
         break;
      case 1:
         std::fprintf(stderr, "ERROR");
         break;
      case 2:
         std::fprintf(stderr, "WARNING");
         break;
      case 3:
         std::fprintf(stderr, "INFO");
         break;
      default:
         std::fprintf(stderr, "LEVEL_%d", level);
         break;
      }
   }
   return result;
}

void wsi_log_message(int level, const char *file, int line, const char *format, ...)
{
   if (check_and_print_log_level(level))
   {
      std::fprintf(stderr, "(%s:%d): ", file, line);
      std::va_list args;
      va_start(args, format);
      std::vfprintf(stderr, format, args);
      va_end(args);
      std::putc('\n', stderr);
   }
}

#endif

} /* namespace util */
