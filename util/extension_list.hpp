/*
 * Copyright (c) 2019, 2021-2022 Arm Limited.
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

#include "custom_allocator.hpp"
#include "helpers.hpp"

#include <vector>
#include <algorithm>

#include <vulkan/vulkan.h>

namespace util
{

/**
 * @brief A helper class for storing a vector of extension names
 *
 * @note This class does not store the extension versions.
 */
class extension_list : private noncopyable
{
public:
   extension_list(const util::allocator &allocator);

   /**
    * @brief Get the allocator used to manage the memory of this object.
    */
   const util::allocator get_allocator() const
   {
      return m_alloc;
   }

   /**
    * @brief Append pointers to extension strings to the given vector.
    *
    * @warning Pointers in the vector are referring to string allocated in this extension_list and will become invalid
    * if the extension_list is modified (e.g. by adding/removing elements.)
    *
    * @param[out] out A vector of C strings to which all extension are appended.
    *
    * @return Indicates whether the operation was successful. If this is @c VK_ERROR_OUT_OF_HOST_MEMORY,
    * then @p out is unmodified.
    */
   VkResult get_extension_strings(util::vector<const char *> &out) const;

   /**
    * @brief Check if this extension list contains all the extensions listed in req.
    */
   bool contains(const extension_list &req) const;

   /**
    * @brief Check if this extension list contains the extension specified by ext.
    */
   bool contains(const char *ext) const;

   /**
    * @brief Remove an extension from a extension list
    */
   void remove(const char *ext);

   VkResult add(VkExtensionProperties ext_prop);
   VkResult add(const VkExtensionProperties *props, size_t count);
   VkResult add(const extension_list &ext_list);
   VkResult add(const char *const *extensions, size_t count);

   VkResult add(const char *extension)
   {
      return add(&extension, 1);
   }

   /**
    * @brief Perform intersection between extensions and add them to the list.
    *
    * Adds the extensions from @p extensions that are also part of @p extensions_subset.
    *
    * @param extensions        A list with the names of the extensions to be added.
    * @param count             Length of the extensions list.
    * @param extensions_subset A list with the names of the target extensions.
    * @param subset_count      Length of the target list.
    *
    * @return VK_SUCCESS on success, otherwise an error.
    */
   VkResult add(const char *const *extensions, size_t count, const char *const *extensions_subset, size_t subset_count);

private:
   util::allocator m_alloc;

   /**
    * @note We are using VkExtensionProperties to store the extension name only
    */
   util::vector<VkExtensionProperties> m_ext_props;
};
} // namespace util
