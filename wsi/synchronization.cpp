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

/**
 * @file
 *
 * @brief Contains the implementation for WSI synchronization primitives.
 */

#include "synchronization.hpp"
#include "layer/private_data.hpp"

namespace wsi
{

fence_sync::fence_sync(layer::device_private_data &device, VkFence vk_fence)
   : fence{ vk_fence }
   , has_payload{ false }
   , dev{ &device }
{
}

util::optional<fence_sync> fence_sync::create(layer::device_private_data &device)
{
   VkFence fence{ VK_NULL_HANDLE };
   VkFenceCreateInfo fence_info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
   VkResult res =
      device.disp.CreateFence(device.device, &fence_info, device.get_allocator().get_original_callbacks(), &fence);
   if (res != VK_SUCCESS)
   {
      return {};
   }
   return fence_sync(device, fence);
}

fence_sync::fence_sync(fence_sync &&rhs)
{
   *this = std::move(rhs);
}

fence_sync &fence_sync::operator=(fence_sync &&rhs)
{
   std::swap(fence, rhs.fence);
   std::swap(has_payload, rhs.has_payload);
   std::swap(payload_finished, rhs.payload_finished);
   std::swap(dev, rhs.dev);
   return *this;
}

fence_sync::~fence_sync()
{
   if (fence != VK_NULL_HANDLE)
   {
      wait_payload(UINT64_MAX);
      dev->disp.DestroyFence(dev->device, fence, dev->get_allocator().get_original_callbacks());
   }
}

VkResult fence_sync::wait_payload(uint64_t timeout)
{
   VkResult res = VK_SUCCESS;
   if (has_payload && !payload_finished)
   {
      res = dev->disp.WaitForFences(dev->device, 1, &fence, VK_TRUE, timeout);
      if (res == VK_SUCCESS)
      {
         payload_finished = true;
      }
   }
   return res;
}

VkResult fence_sync::set_payload(VkQueue queue, const VkSemaphore *sem_payload, uint32_t sem_count)
{
   VkResult result = dev->disp.ResetFences(dev->device, 1, &fence);
   if (result != VK_SUCCESS)
   {
      return result;
   }
   has_payload = false;
   /* When the semaphore that comes in is signalled, we know that all work is done. So, we do not
    * want to block any future Vulkan queue work on it. So, we pass in BOTTOM_OF_PIPE bit as the
    * wait flag.
    */
   VkPipelineStageFlags pipeline_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

   VkSubmitInfo submit_info = {
      VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, sem_count, sem_payload, &pipeline_stage_flags, 0, nullptr, 0, nullptr
   };

   result = dev->disp.QueueSubmit(queue, 1, &submit_info, fence);
   if (result == VK_SUCCESS)
   {
      has_payload = true;
      payload_finished = false;
   }
   return result;
}

bool fence_sync::swap_payload(bool new_payload)
{
   bool old_payload = has_payload;
   has_payload = new_payload;
   payload_finished = false;
   return old_payload;
}

sync_fd_fence_sync::sync_fd_fence_sync(layer::device_private_data &device, VkFence vk_fence)
   : fence_sync{ device, vk_fence }
{
}

bool sync_fd_fence_sync::is_supported(layer::instance_private_data &instance, VkPhysicalDevice phys_dev)
{
   VkPhysicalDeviceExternalFenceInfoKHR external_fence_info = {};
   external_fence_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO;
   external_fence_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   VkExternalFencePropertiesKHR fence_properties = {};
   fence_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
   instance.disp.GetPhysicalDeviceExternalFencePropertiesKHR(phys_dev, &external_fence_info, &fence_properties);
   return fence_properties.externalFenceFeatures & VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT_KHR;
}

util::optional<sync_fd_fence_sync> sync_fd_fence_sync::create(layer::device_private_data &device)
{
   VkExportFenceCreateInfo export_fence_create_info = {};
   export_fence_create_info.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
   export_fence_create_info.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, &export_fence_create_info, 0 };
   VkFence fence = VK_NULL_HANDLE;
   VkResult res =
      device.disp.CreateFence(device.device, &fence_info, device.get_allocator().get_original_callbacks(), &fence);
   if (res != VK_SUCCESS)
   {
      return {};
   }
   return sync_fd_fence_sync{ device, fence };
}

util::optional<util::fd_owner> sync_fd_fence_sync::export_sync_fd()
{
   int exported_fd = -1;
   VkFenceGetFdInfoKHR fence_fd_info = {};
   fence_fd_info.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
   fence_fd_info.fence = get_fence();
   fence_fd_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

   VkResult result = get_device().disp.GetFenceFdKHR(get_device().device, &fence_fd_info, &exported_fd);
   if (result == VK_SUCCESS)
   {
      swap_payload(false);
      return util::fd_owner(exported_fd);
   }
   return {};
}

} /* namespace wsi */
