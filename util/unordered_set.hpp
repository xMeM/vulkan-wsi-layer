/*
 * Copyright (c) 2021-2022, 2024 Arm Limited.
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

#include <optional>
#include <unordered_set>

#include "custom_allocator.hpp"
#include "helpers.hpp"

namespace util
{
/**
 * @brief This utility class has the same purpose as std::unordered_set, but
 *        ensures that the operations that could result in out of memory
 *        exceptions don't throw them and also ensures that the memory can
 *        only be allocated by an custom_allocator.
 */
template <typename Key, typename Hash = std::hash<Key>, typename Comparator = std::equal_to<Key>,
          typename Allocator = util::custom_allocator<Key>>
class unordered_set : public std::unordered_set<Key, Hash, Comparator, Allocator>, private noncopyable
{
   using value_type = Key;
   using base = std::unordered_set<Key, Hash, Comparator, Allocator>;
   using size_type = typename base::size_type;
   using iterator = typename base::iterator;

public:
   /**
    * Delete all member functions that can cause allocation failure by throwing std::bad_alloc.
    */
   void insert() = delete;
   void emplace() = delete;
   void emplace_hint() = delete;
   void rehash() = delete;
   void reserve() = delete;

   /**
    * @brief Construct a new unordered set object with a custom allocator.
    *
    * @param allocator The allocator that will be used.
    */
   explicit unordered_set(util::custom_allocator<Key> allocator)
      : base(allocator)
   {
   }

   /**
    * @brief Like std::unordered_set.insert but doesn't throw on out of memory errors.
    *
    * @param value The value to insert in the map.
    * @return std::optional<std::pair<iterator,bool>> If successful, the optional will
    *         contain the same return value as from std::unordered_set.insert, otherwise
    *         if out of memory, the function returns std::nullopt.
    */
   std::optional<std::pair<iterator, bool>> try_insert(const value_type &value) noexcept
   {
      try
      {
         return { base::insert(value) };
      }
      catch (const std::bad_alloc &e)
      {
         return std::nullopt;
      }
   }

   /**
    * @brief Like std::unordered_set.reserve but doesn't throw on out of memory errors.
    *
    * @param size The new capacity of the container. Same as std::unordered_set.reserve.
    * @return true If the container was resized successfuly.
    * @return false If the host has run out of memory
    */
   bool try_reserve(size_type size)
   {
      try
      {
         base::reserve(size);
         return true;
      }
      catch (std::bad_alloc &e)
      {
         return false;
      }
   }

   /**
    * @brief Like std::unordered_set.rehash but doesn't throw on out of memory errors.
    *
    * @param count Number of buckets. Same as std::unordered_set.rehash.
    * @return true If the container was rehashed successfuly.
    * @return false If the host has run out of memory
    */
   bool try_rehash(size_type count)
   {
      try
      {
         base::rehash(count);
         return true;
      }
      catch (std::bad_alloc &e)
      {
         return false;
      }
   }
};
} // namespace util
