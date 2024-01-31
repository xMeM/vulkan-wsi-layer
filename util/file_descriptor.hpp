/*
 * Copyright (c) 2021, 2024 Arm Limited.
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
 *
 * @brief Contains the defintions of file descriptor utilities.
 */

#pragma once

#include <unistd.h>
#include <utility>

#include "helpers.hpp"

namespace util
{

/**
 * Manages a POSIX file descriptor.
 */
class fd_owner : private noncopyable
{
public:
   fd_owner() = default;
   fd_owner(int fd)
      : fd_handle{ fd }
   {
   }

   fd_owner(fd_owner &&rhs)
   {
      *this = std::move(rhs);
   }

   fd_owner &operator=(fd_owner &&rhs)
   {
      std::swap(fd_handle, rhs.fd_handle);
      return *this;
   }

   ~fd_owner()
   {
      if (is_valid())
      {
         close(fd_handle);
      }
   }

   int get() const
   {
      return fd_handle;
   }

   bool is_valid() const
   {
      return fd_handle >= 0;
   }

private:
   int fd_handle{ -1 };
};

} /* namespace util */
