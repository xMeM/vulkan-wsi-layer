/*
 * Copyright (c) 2021-2024 Arm Limited.
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
 * @brief Contains the defintions of WSI synchronization primitives.
 */

#pragma once

#include <optional>

#include "util/file_descriptor.hpp"

#include <vulkan/vulkan.h>

namespace layer
{
class device_private_data;
class instance_private_data;
} /* namespace layer */

namespace wsi
{

struct queue_submit_semaphores
{
   const VkSemaphore *wait_semaphores;
   uint32_t wait_semaphores_count;
   const VkSemaphore *signal_semaphores;
   uint32_t signal_semaphores_count;
};

/**
 * Synchronization using a Vulkan Fence object.
 */
class fence_sync
{
public:
   /**
    * Creates a new fence synchronization object.
    *
    * @param device The device private data for which to create it.
    *
    * @return Empty optional on failure or initialized fence.
    */
   static std::optional<fence_sync> create(layer::device_private_data &device);

   fence_sync() = default;
   fence_sync(const fence_sync &) = delete;
   fence_sync &operator=(const fence_sync &) = delete;

   fence_sync(fence_sync &&rhs);
   fence_sync &operator=(fence_sync &&rhs);

   virtual ~fence_sync();

   /**
    * Waits for any pending payload to complete execution.
    *
    * @note This method is not threadsafe.
    *
    * @param timeout Timeout for waiting in nanoseconds.
    *
    * @return VK_SUCCESS on success or if no payload or a completed payload is set.
    *         Other error code on failure or timeout.
    */
   VkResult wait_payload(uint64_t timeout);

   /**
    * Sets the payload for the fence that would need to complete before operations that wait on it.
    *
    * @note This method is not threadsafe.
    *
    * @param     queue  The Vulkan queue that may be used to submit synchronization commands.
    * @param semaphores The wait and signal semaphores.
    *
    * @return VK_SUCCESS on success or other error code on failing to set the payload.
    */
   VkResult set_payload(VkQueue queue, const queue_submit_semaphores &semaphores);

protected:
   /**
    * Non-public constructor to initialize the object with valid data.
    *
    * @param device   The device private data for the fence.
    * @param vk_fence The created Vulkan fence.
    */
   fence_sync(layer::device_private_data &device, VkFence vk_fence);

   VkFence get_fence()
   {
      return fence;
   }

   /**
    * Swaps current payload. This operation could be performed when exporting or importing external fences.
    *
    * @param new_payload Whether a new payload is set.
    *
    * @return If there is an existing payload that is being replaced.
    */
   bool swap_payload(bool new_payload);

   layer::device_private_data &get_device()
   {
      return *dev;
   }

private:
   VkFence fence{ VK_NULL_HANDLE };
   bool has_payload{ false };
   bool payload_finished{ false };
   layer::device_private_data *dev{ nullptr };
};

/**
 * Synchronization using a Vulkan fence exportable to a native Sync FD object.
 */
class sync_fd_fence_sync : public fence_sync
{
public:
   sync_fd_fence_sync() = default;

   /**
    * Checks if a Vulkan device can support Sync FD fences.
    *
    * @param instance The instance private data for the physical device.
    * @param phys_dev The physical device to check support for.
    *
    * @return true if supported, false otherwise.
    */
   static bool is_supported(layer::instance_private_data &instance, VkPhysicalDevice phys_dev);

   /**
    * Creates a new fence compatible with Sync FD.
    *
    * @param device The device private data for which to create the fence.
    *
    * @return Empty optional on failure or initialized fence.
    */
   static std::optional<sync_fd_fence_sync> create(layer::device_private_data &device);

   /**
    * Exports the fence to a native Sync FD.
    *
    * @note This method is not threadsafe.
    *
    * @return The exported Sync FD on success or empty optional on failure.
    */
   std::optional<util::fd_owner> export_sync_fd();

private:
   /**
    * Non-public constructor to initialize the object with valid data.
    *
    * @param device   The device private data for the fence.
    * @param vk_fence The created exportable Vulkan fence.
    */
   sync_fd_fence_sync(layer::device_private_data &device, VkFence vk_fence);
};

/**
 * @brief Submit an empty queue operation for synchronization.
 *
 * @param device     The device private data for the fence.
 * @param queue      The Vulkan queue that may be used to submit synchronization commands.
 * @param fence      The fence to be signalled, it could be VK_NULL_HANDLE in the absence
 *                   of a fence to be signalled.
 * @param semaphores The wait and signal semaphores.
 *
 * @return VK_SUCCESS on success, an appropiate error code otherwise.
 */
VkResult sync_queue_submit(const layer::device_private_data &device, VkQueue queue, VkFence fence,
                           const queue_submit_semaphores &semaphores);
} /* namespace wsi */
