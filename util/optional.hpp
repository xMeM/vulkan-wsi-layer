/*
 * Copyright (c) 2021 Arm Limited.
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
#include <cassert>

namespace util
{
template <typename T>
class optional
{
public:
   using value_type = T;

   optional(const optional &) = delete;
   optional &operator=(const optional &) = delete;

   /**
    * @brief Construct an empty optional object.
    */
   optional() = default;

   /**
    * @brief Construct an optional object with a value.
    *
    * @param val The value that will be placed in the optional.
    */
   optional(value_type &&val) noexcept
      : m_has_value(true), m_value(std::move(val))
   {
   }

   /**
    * @brief Construct a new optional object from another optional
    *
    * @param opt The optional object that will be moved
    */
   optional(optional &&opt)
      : m_has_value(opt.m_has_value)
   {
      if (opt.m_has_value)
      {
         m_value = std::move(opt.m_value);
         opt.m_has_value = false;
      }
      else
      {
         m_value = T{};
      }
   }

   /**
    * @brief Check if optional has a value.
    *
    * @return true If the optional has a value.
    * @return false If the optional does not have a value.
    */
   bool has_value() const noexcept
   {
      return m_has_value;
   }

   /**
    * @brief Return the value in the optional. It is only
    *        valid to call this function if optional has a value.
    *
    * @return value_type& The value that is in the optional
    */
   value_type &value() noexcept
   {
      assert(has_value());
      return m_value;
   }

   /**
    * @brief Clears the value from the optional
    *
    */
   void reset() noexcept
   {
      m_has_value = false;
      m_value = T{};
   }

   /**
    * @brief Reassign/assign the value in the optional.
    *
    * @return optional& This optional object with the value.
    */
   optional &set(T &&val) noexcept
   {
      m_value = std::move(val);
      m_has_value = true;
      return *this;
   }

   optional &set(const T &val) noexcept
   {
      m_value = val;
      m_has_value = true;
      return *this;
   }

   /**
    * @brief Return the value in the optional, same as value()
    *
    * @return value_type& The value in the optional
    */
   value_type &operator*() noexcept
   {
      assert(has_value());
      return m_value;
   }

   /**
    * @brief Return the value in the optional as pointer.
    *
    * @return value_type* The value in the optional
    */
   value_type *operator->() noexcept
   {
      assert(has_value());
      return &m_value;
   }

   /**
    * @brief Reassign/assign the value in the optional
    *
    * @param val The value to assign to this optional
    * @return optional& This optional object with the value
    */
   optional &operator=(value_type &&val) noexcept
   {
      m_has_value = true;
      m_value = std::move(val);

      return *this;
   }

   /**
    * @brief Construct a new optional object from another optional
    *
    * @param opt The optional object that will be moved
    * @return optional& This optional object with the value
    */
   optional &operator=(optional &&opt)
   {
      if (this != &opt)
      {
         if (opt.m_has_value)
         {
            m_has_value = true;
            m_value = std::move(opt.m_value);
            opt.m_has_value = false;
         }
         else
         {
            m_value = T{};
            m_has_value = false;
         }
      }

      return *this;
   }

private:
   bool m_has_value{false};
   T m_value{};
};

template <typename T, typename... Args>
inline optional<T> make_optional(Args &&...args)
{
   return optional<T>{T(std::forward<Args>(args)...)};
}
}