/*
 * Copyright (c) 2022 Arm Limited.
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
 * @file external_memory.hpp
 *
 * @brief Handles importing and binding external memory for swapchain implementations.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

#include "wsi/synchronization.hpp"
#include "layer/private_data.hpp"
#include "util/custom_allocator.hpp"
#include "util/helpers.hpp"

namespace wsi
{

using util::MAX_PLANES;

class external_memory
{
public:
   external_memory(const VkDevice &device, const util::allocator &allocator);
   ~external_memory();

   /**
    * @brief Get the fds representing the externally created memory for each plane.
    */
   const std::array<int, MAX_PLANES> &get_buffer_fds(void)
   {
      return m_buffer_fds;
   }

   /**
    * @brief Get the per plane stride values.
    */
   const std::array<int, MAX_PLANES> &get_strides(void)
   {
      return m_strides;
   }

   /**
    * @brief Get the per plane offset values.
    */
   const std::array<uint32_t, MAX_PLANES> &get_offsets(void)
   {
      return m_offsets;
   }

   /**
    * @brief Set the per plane fd values.
    */
   void set_buffer_fds(std::array<int, MAX_PLANES> buffer_fds)
   {
      m_buffer_fds = buffer_fds;
   }

   /**
    * @brief Set the per plane stride values.
    */
   void set_strides(std::array<int, MAX_PLANES> strides)
   {
      m_strides = strides;
   }

   /**
    * @brief Set the per plane offset values.
    */
   void set_offsets(std::array<uint32_t, MAX_PLANES> offsets)
   {
      m_offsets = offsets;
   }

   /**
    * @brief Get the number of planes the external format uses.
    */
   uint32_t get_num_planes(void);

   /**
    * @brief Returns whether the external memory uses a multi-planar format where each plane is separately bound to
    *        memory.
    */
   bool is_disjoint(void);

   /**
    * @brief Set the external memory type.
    */
   void set_memory_handle_type(VkExternalMemoryHandleTypeFlagBits handle_type)
   {
      m_handle_type = handle_type;
   }

   /**
    * @brief Binds the external memory to a swapchain image.
    *
    * Must only be used after the external memory has been imported.
    *
    * @param image     The swapchain image.
    *
    * @return VK_ERROR_OUT_OF_HOST_MEMORY when out of memory else returns the result of
    *         VkBindImageMemory/VkBindImageMemory2KHR.
    */
   VkResult bind_swapchain_image_memory(const VkImage &image);

   /**
    * @brief Imports the externally allocated memory into the Vulkan framework and binds it to a swapchain image.
    *
    * @param image     The swapchain image.
    *
    * @return VK_ERROR_OUT_OF_HOST_MEMORY when out of memory. Other possible values include the result of calling
    *         VkAllocateMemory/VkBindImageMemory/VkBindImageMemory2KHR.
    */
   VkResult import_memory_and_bind_swapchain_image(const VkImage &image);

   /**
    * @brief Fills out a list of VkSubresourceLayout for each plane using the stored planes layout data.
    *
    * @param image_plane_layouts    A list of plane layouts to fill.
    *
    * @return VK_ERROR_OUT_OF_HOST_MEMORY when out of memory else VK_SUCCESS on success.
    */
   VkResult fill_image_plane_layouts(util::vector<VkSubresourceLayout> &image_plane_layouts);

   /**
    * @brief Fills out a VkImageDrmFormatModifierExplicitCreateInfoEXT struct.
    *
    * @param pNext         Pointer to the pNext chain to set the VkImageDrmFormatModifierExplicitCreateInfoEXT struct to.
    * @param drm_mod_info  The struct to fill out.
    * @param image_layouts Per plane image layouts
    * @param modifier      Modifier that the DRM format will use.
    */
   void fill_drm_mod_info(const void *pNext, VkImageDrmFormatModifierExplicitCreateInfoEXT &drm_mod_info,
                          util::vector<VkSubresourceLayout> &image_layout, uint64_t modifier);

   /**
    * @brief Fills out a VkExternalMemoryImageCreateInfoKHR struct.
    *
    * @param external_info The struct to fill out.
    * @param pNext         Pointer to the pNext chain to set the VkExternalMemoryImageCreateInfoKHR struct to.
    */
   void fill_external_info(VkExternalMemoryImageCreateInfoKHR &external_info, void *pNext);

private:
   VkResult get_fd_mem_type_index(uint32_t index, uint32_t *mem_idx);

   VkResult import_plane_memories(void);

   VkResult import_plane_memory(uint32_t index);

   std::array<int, MAX_PLANES> m_buffer_fds{ -1, -1, -1, -1 };
   std::array<int, MAX_PLANES> m_strides{ 0, 0, 0, 0 };
   std::array<uint32_t, MAX_PLANES> m_offsets{ 0, 0, 0, 0 };
   std::array<VkDeviceMemory, MAX_PLANES> m_memories = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                         VK_NULL_HANDLE };
   uint32_t m_num_planes{ 0 };
   VkExternalMemoryHandleTypeFlagBits m_handle_type{ VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
   const VkDevice &m_device;
   const util::allocator &m_allocator;
};

} // namespace wsi
