/*
 * Copyright (c) 2017-2021 Arm Limited.
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
 * @file swapchain.cpp
 *
 * @brief Contains the implementation for a headless swapchain.
 */

#include <cassert>
#include <cstdlib>

#include <util/timed_semaphore.hpp>

#include "swapchain.hpp"

namespace wsi
{
namespace headless
{

struct image_data
{
   /* Device memory backing the image. */
   VkDeviceMemory memory{};
   fence_sync present_fence;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_image_create_info()
{
}

swapchain::~swapchain()
{
   /* Call the base's teardown */
   teardown();
}

VkResult swapchain::create_aliased_image_handle(const VkImageCreateInfo *image_create_info, VkImage *image)
{
   return m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), image);
}

VkResult swapchain::create_and_bind_swapchain_image(VkImageCreateInfo image_create, wsi::swapchain_image &image)
{
   VkResult res = VK_SUCCESS;
   const std::lock_guard<std::recursive_mutex> lock(m_image_status_mutex);

   m_image_create_info = image_create;
   res = m_device_data.disp.CreateImage(m_device, &image_create, get_allocation_callbacks(), &image.image);
   if (res != VK_SUCCESS)
   {
      return res;
   }

   VkMemoryRequirements memory_requirements;
   m_device_data.disp.GetImageMemoryRequirements(m_device, image.image, &memory_requirements);

   /* Find a memory type */
   size_t mem_type_idx = 0;
   for (; mem_type_idx < 8 * sizeof(memory_requirements.memoryTypeBits); ++mem_type_idx)
   {
      if (memory_requirements.memoryTypeBits & (1u << mem_type_idx))
      {
         break;
      }
   }

   assert(mem_type_idx <= 8 * sizeof(memory_requirements.memoryTypeBits) - 1);

   VkMemoryAllocateInfo mem_info = {};
   mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mem_info.allocationSize = memory_requirements.size;
   mem_info.memoryTypeIndex = mem_type_idx;
   image_data *data = nullptr;

   /* Create image_data */
   data = m_allocator.create<image_data>(1);
   if (data == nullptr)
   {
      m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image.data = reinterpret_cast<void *>(data);
   image.status = wsi::swapchain_image::FREE;

   res = m_device_data.disp.AllocateMemory(m_device, &mem_info, get_allocation_callbacks(), &data->memory);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   res = m_device_data.disp.BindImageMemory(m_device, image.image, data->memory, 0);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   /* Initialize presentation fence. */
   auto present_fence = fence_sync::create(m_device_data);
   if (!present_fence.has_value())
   {
      destroy_image(image);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   data->present_fence = std::move(present_fence.value());

   return res;
}

void swapchain::present_image(uint32_t pending_index)
{
   unpresent_image(pending_index);
}

void swapchain::destroy_image(wsi::swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (image.status != wsi::swapchain_image::INVALID)
   {
      if (image.image != VK_NULL_HANDLE)
      {
         m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
         image.image = VK_NULL_HANDLE;
      }

      image.status = wsi::swapchain_image::INVALID;
   }

   image_status_lock.unlock();

   if (image.data != nullptr)
   {
      auto *data = reinterpret_cast<image_data *>(image.data);
      if (data->memory != VK_NULL_HANDLE)
      {
         m_device_data.disp.FreeMemory(m_device, data->memory, get_allocation_callbacks());
         data->memory = VK_NULL_HANDLE;
      }
      m_allocator.destroy(1, data);
      image.data = nullptr;
   }

}

VkResult swapchain::image_set_present_payload(swapchain_image &image, VkQueue queue, const VkSemaphore *sem_payload,
                                              uint32_t sem_count)
{
   auto data = reinterpret_cast<image_data *>(image.data);
   return data->present_fence.set_payload(queue, sem_payload, sem_count);
}

VkResult swapchain::image_wait_present(swapchain_image &image, uint64_t timeout)
{
   auto data = reinterpret_cast<image_data *>(image.data);
   return data->present_fence.wait_payload(timeout);
}

VkResult swapchain::bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info)
{
   auto &device_data = layer::device_private_data::get(device);

   const wsi::swapchain_image &swapchain_image = m_swapchain_images[bind_sc_info->imageIndex];
   VkDeviceMemory memory = reinterpret_cast<image_data *>(swapchain_image.data)->memory;

   return device_data.disp.BindImageMemory(device, bind_image_mem_info->image, memory, 0);
}

} /* namespace headless */
} /* namespace wsi */
