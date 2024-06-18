/*
 * Copyright (c) 2022-2024 Arm Limited.
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

#include "external_memory.hpp"

#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <algorithm>

#include "util/log.hpp"
#include "util/helpers.hpp"
#include "util/drm/drm_utils.hpp"

namespace wsi
{

external_memory::external_memory(const VkDevice &device, const util::allocator &allocator)
   : m_device(device)
   , m_allocator(allocator)
{
}

external_memory::~external_memory()
{
   auto &device_data = layer::device_private_data::get(m_device);
   for (uint32_t plane = 0; plane < get_num_planes(); plane++)
   {
      auto &memory = m_memories[plane];
      if (memory != VK_NULL_HANDLE)
      {
         device_data.disp.FreeMemory(m_device, memory, m_allocator.get_original_callbacks());
      }
      else if (m_buffer_fds[plane] >= 0)
      {
         auto it = std::find(std::begin(m_buffer_fds), std::end(m_buffer_fds), m_buffer_fds[plane]);
         if (std::distance(std::begin(m_buffer_fds), it) == plane)
         {
            close(m_buffer_fds[plane]);
         }
      }
   }
}

uint32_t external_memory::get_num_planes()
{
   return m_num_planes;
}

uint32_t external_memory::get_num_memories()
{
   return m_num_memories;
}

bool external_memory::is_disjoint()
{
   return m_num_memories != 1;
}

VkResult external_memory::get_fd_mem_type_index(int fd, uint32_t *mem_idx)
{
   auto &device_data = layer::device_private_data::get(m_device);
   VkMemoryFdPropertiesKHR mem_props = {};
   mem_props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;

   TRY_LOG(device_data.disp.GetMemoryFdPropertiesKHR(m_device, m_handle_type, fd, &mem_props),
           "Error querying file descriptor properties");

   for (*mem_idx = 0; *mem_idx < VK_MAX_MEMORY_TYPES; (*mem_idx)++)
   {
      if (mem_props.memoryTypeBits & (1 << *mem_idx))
      {
         break;
      }
   }

   assert(*mem_idx < VK_MAX_MEMORY_TYPES);

   return VK_SUCCESS;
}

VkResult external_memory::import_plane_memories()
{
   if (is_disjoint())
   {
      uint32_t memory_plane = 0;
      for (uint32_t plane = 0; plane < get_num_planes(); plane++)
      {
         auto it = std::find(std::begin(m_buffer_fds), std::end(m_buffer_fds), m_buffer_fds[plane]);
         if (std::distance(std::begin(m_buffer_fds), it) == plane)
         {
            TRY_LOG_CALL(import_plane_memory(m_buffer_fds[plane], &m_memories[memory_plane]));
            memory_plane++;
         }
      }
      return VK_SUCCESS;
   }
   return import_plane_memory(m_buffer_fds[0], &m_memories[0]);
}

VkResult external_memory::import_plane_memory(int fd, VkDeviceMemory *memory)
{
   uint32_t mem_index = 0;
   TRY_LOG_CALL(get_fd_mem_type_index(fd, &mem_index));

   const off_t fd_size = lseek(fd, 0, SEEK_END);
   if (fd_size < 0)
   {
      WSI_LOG_ERROR("Failed to get fd size.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   VkImportMemoryFdInfoKHR import_mem_info = {};
   import_mem_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
   import_mem_info.handleType = m_handle_type;
   import_mem_info.fd = fd;

   VkMemoryAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.pNext = &import_mem_info;
   alloc_info.allocationSize = static_cast<uint64_t>(fd_size);
   alloc_info.memoryTypeIndex = mem_index;

   auto &device_data = layer::device_private_data::get(m_device);
   TRY_LOG(device_data.disp.AllocateMemory(m_device, &alloc_info, m_allocator.get_original_callbacks(), memory),
           "Failed to import device memory");

   return VK_SUCCESS;
}

VkResult external_memory::bind_swapchain_image_memory(const VkImage &image)
{
   auto &device_data = layer::device_private_data::get(m_device);
   if (is_disjoint())
   {
      util::vector<VkBindImageMemoryInfo> bind_img_mem_infos(m_allocator);
      if (!bind_img_mem_infos.try_resize(get_num_memories()))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      util::vector<VkBindImagePlaneMemoryInfo> bind_plane_mem_infos(m_allocator);
      if (!bind_plane_mem_infos.try_resize(get_num_memories()))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      for (uint32_t plane = 0; plane < get_num_memories(); plane++)
      {
         bind_plane_mem_infos[plane].planeAspect = util::PLANE_FLAG_BITS[plane];
         bind_plane_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
         bind_plane_mem_infos[plane].pNext = nullptr;

         bind_img_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
         bind_img_mem_infos[plane].pNext = &bind_plane_mem_infos[plane];
         bind_img_mem_infos[plane].image = image;
         bind_img_mem_infos[plane].memory = m_memories[plane];
         bind_img_mem_infos[plane].memoryOffset = m_offsets[plane];
      }

      return device_data.disp.BindImageMemory2KHR(m_device, bind_img_mem_infos.size(), bind_img_mem_infos.data());
   }

   return device_data.disp.BindImageMemory(m_device, image, m_memories[0], m_offsets[0]);
}

VkResult external_memory::import_memory_and_bind_swapchain_image(const VkImage &image)
{
   TRY_LOG_CALL(import_plane_memories());
   TRY_LOG_CALL(bind_swapchain_image_memory(image));
   return VK_SUCCESS;
}

VkResult external_memory::fill_image_plane_layouts(util::vector<VkSubresourceLayout> &image_plane_layouts)
{
   if (!image_plane_layouts.try_resize(get_num_planes()))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t plane = 0; plane < get_num_planes(); plane++)
   {
      assert(m_strides[plane] >= 0);
      image_plane_layouts[plane].offset = m_offsets[plane];
      image_plane_layouts[plane].rowPitch = static_cast<uint32_t>(m_strides[plane]);
   }
   return VK_SUCCESS;
}

void external_memory::fill_drm_mod_info(const void *pNext, VkImageDrmFormatModifierExplicitCreateInfoEXT &drm_mod_info,
                                        util::vector<VkSubresourceLayout> &plane_layouts, uint64_t modifier)
{
   drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   drm_mod_info.pNext = pNext;
   drm_mod_info.drmFormatModifier = modifier;
   drm_mod_info.drmFormatModifierPlaneCount = get_num_memories();
   drm_mod_info.pPlaneLayouts = plane_layouts.data();
}

void external_memory::fill_external_info(VkExternalMemoryImageCreateInfoKHR &external_info, void *pNext)
{
   external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
   external_info.pNext = pNext;
   external_info.handleTypes = m_handle_type;
}

} // namespace wsi
