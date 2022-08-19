/*
 * Copyright (c) 2021-2022 Arm Limited.
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

#include <array>
#include <memory>
#include <optional>

namespace util
{

template <typename T, std::size_t N>
class ring_buffer
{
public:
   /**
    * @brief Return maximum capacity of the ring buffer.
    */
   constexpr std::size_t capacity()
   {
      return N;
   }

   /**
    * @brief Return current size of the ring buffer.
    */
   std::size_t size() const
   {
      return m_size;
   }

   /**
    * @brief Places item into next slot of the ring buffer.
    * @return Boolean to indicate success or failure.
    */
   template <typename U>
   bool push_back(U &&item)
   {
      if (size() == capacity())
      {
         return false;
      }

      m_data[(m_begin + m_size) % N].emplace(std::forward<U>(item));
      ++m_size;

      return true;
   }

   /**
    * @brief Gets a pointer to the item at the starting index of the ring buffer.
    */
   T *front()
   {
      return get(m_begin);
   }

   /**
    * @brief Gets a pointer to the item that was last placed into the ring buffer.
    */
   T *back()
   {
      return get((m_begin + m_size + N - 1) % N);
   }

   /**
    * @brief Pop the front of the ring buffer.
    *
    * Item at the starting index of the ring buffer is returned. The slot is subsequently emptied. The starting index of
    * the ring buffer increments by 1.
    *
    * @return Item wrapped in an optional.
    */
   std::optional<T> pop_front()
   {
      if (size() == 0)
      {
         return std::nullopt;
      }

      std::optional<T> value = std::move(m_data[m_begin]);

      m_begin = (m_begin + 1) % N;
      --m_size;

      return value;
   }

private:
   T *get(std::size_t index)
   {
      if (m_data[index].has_value())
      {
         return std::addressof(m_data[index].value());
      }
      else
      {
         return nullptr;
      }
   }

   std::array<std::optional<T>, N> m_data{};

   // Marks the start index of the ring buffer.
   std::size_t m_begin{};

   // The number of entries in the ring buffer from the start index.
   std::size_t m_size{};
};

} /* namespace util */
