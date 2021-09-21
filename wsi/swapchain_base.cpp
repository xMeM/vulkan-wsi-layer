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
 * @file swapchain_base.cpp
 *
 * @brief Contains the implementation for the swapchain.
 *
 * This file contains much of the swapchain implementation,
 * that is not specific to how images are created or presented.
 */

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <unistd.h>
#include <vulkan/vulkan.h>

#include "util/log.hpp"

#include "swapchain_base.hpp"
namespace wsi
{

void swapchain_base::page_flip_thread()
{
   auto &sc_images = m_swapchain_images;
   VkResult vk_res = VK_SUCCESS;
   uint64_t timeout = UINT64_MAX;
   constexpr uint64_t SEMAPHORE_TIMEOUT = 250000000; /* 250 ms. */

   /* No mutex is needed for the accesses to m_page_flip_thread_run variable as after the variable is
    * initialized it is only ever changed to false. The while loop will make the thread read the
    * value repeatedly, and the combination of semaphores and thread joins will force any changes to
    * the variable to be visible to this thread.
    */
   while (m_page_flip_thread_run)
   {
      /* Waiting for the page_flip_semaphore which will be signalled once there is an
       * image to display.*/
      if ((vk_res = m_page_flip_semaphore.wait(SEMAPHORE_TIMEOUT)) == VK_TIMEOUT)
      {
         /* Image is not ready yet. */
         continue;
      }
      assert(vk_res == VK_SUCCESS);

      /* We want to present the oldest queued for present image from our present queue,
       * which we can find at the sc->pending_buffer_pool.head index. */
      auto pending_index = m_pending_buffer_pool.pop_front();
      assert(pending_index.has_value());

      /* We may need to wait for the payload of the present sync of the oldest pending image to be finished. */
      vk_res = image_wait_present(sc_images[*pending_index], timeout);
      if (vk_res != VK_SUCCESS)
      {
         m_is_valid = false;
         m_free_image_semaphore.post();
         continue;
      }

      /* First present of the swapchain. If it has an ancestor, wait until all the pending buffers
       * from the ancestor have finished page flipping before we set mode. */
      if (m_first_present)
      {
         if (m_ancestor != VK_NULL_HANDLE)
         {
            auto *ancestor = reinterpret_cast<swapchain_base *>(m_ancestor);
            ancestor->wait_for_pending_buffers();
         }

         sem_post(&m_start_present_semaphore);

         present_image(*pending_index);

         m_first_present = false;
      }
      /* The swapchain has already started presenting. */
      else
      {
         present_image(*pending_index);
      }
   }
}

void swapchain_base::unpresent_image(uint32_t presented_index)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   m_swapchain_images[presented_index].status = swapchain_image::FREE;

   image_status_lock.unlock();
   m_free_image_semaphore.post();
}

swapchain_base::swapchain_base(layer::device_private_data &dev_data, const VkAllocationCallbacks *callbacks)
   : m_device_data(dev_data)
   , m_page_flip_thread_run(true)
   , m_thread_sem_defined(false)
   , m_first_present(true)
   , m_pending_buffer_pool{}
   , m_allocator(dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, callbacks)
   , m_swapchain_images(m_allocator)
   , m_surface(VK_NULL_HANDLE)
   , m_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
   , m_descendant(VK_NULL_HANDLE)
   , m_ancestor(VK_NULL_HANDLE)
   , m_device(VK_NULL_HANDLE)
   , m_queue(VK_NULL_HANDLE)
   , m_image_acquire_lock()
{
}

VkResult swapchain_base::init(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   assert(device != VK_NULL_HANDLE);
   assert(swapchain_create_info != nullptr);
   assert(swapchain_create_info->surface != VK_NULL_HANDLE);

   int res;
   VkResult result;

   m_device = device;
   m_surface = swapchain_create_info->surface;

   /* Check presentMode has a compatible value with swapchain - everything else should be taken care at image creation.*/
   static const std::array<VkPresentModeKHR, 2> present_modes = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR };
   bool present_mode_found = false;
   for (uint32_t i = 0; i < present_modes.size() && !present_mode_found; i++)
   {
      if (swapchain_create_info->presentMode == present_modes[i])
      {
         present_mode_found = true;
      }
   }

   if (!present_mode_found)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_present_mode = swapchain_create_info->presentMode;

   /* Init image to invalid values. */
   if (!m_swapchain_images.try_resize(swapchain_create_info->minImageCount))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* We have allocated images, we can call the platform init function if something needs to be done. */
   result = init_platform(device, swapchain_create_info);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   VkImageCreateInfo image_create_info = {};
   image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_create_info.pNext = nullptr;
   image_create_info.imageType = VK_IMAGE_TYPE_2D;
   image_create_info.format = swapchain_create_info->imageFormat;
   image_create_info.extent = { swapchain_create_info->imageExtent.width, swapchain_create_info->imageExtent.height, 1 };
   image_create_info.mipLevels = 1;
   image_create_info.arrayLayers = swapchain_create_info->imageArrayLayers;
   image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_create_info.usage = swapchain_create_info->imageUsage;
   image_create_info.flags = 0;
   image_create_info.sharingMode = swapchain_create_info->imageSharingMode;
   image_create_info.queueFamilyIndexCount = swapchain_create_info->queueFamilyIndexCount;
   image_create_info.pQueueFamilyIndices = swapchain_create_info->pQueueFamilyIndices;
   image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   result = m_free_image_semaphore.init(m_swapchain_images.size());
   if (result != VK_SUCCESS)
   {
      assert(result == VK_ERROR_OUT_OF_HOST_MEMORY);
      return result;
   }

   for (auto& img : m_swapchain_images)
   {
      result = create_and_bind_swapchain_image(image_create_info, img);
      if (result != VK_SUCCESS)
      {
         return result;
      }
   }

   m_device_data.disp.GetDeviceQueue(m_device, 0, 0, &m_queue);
   result = m_device_data.SetDeviceLoaderData(m_device, m_queue);
   if (VK_SUCCESS != result)
   {
      return result;
   }

   /* Setup semaphore for signaling pageflip thread */
   result = m_page_flip_semaphore.init(0);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   res = sem_init(&m_start_present_semaphore, 0, 0);
   /* Only programming error can cause this to fail. */
   assert(res == 0);
   if (res != 0)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   m_thread_sem_defined = true;

   /* Launch page flipping thread */
   m_page_flip_thread = std::thread(&swapchain_base::page_flip_thread, this);

   /* Release the swapchain images of the old swapchain in order
    * to free up memory for new swapchain. This is necessary especially
    * on platform with limited display memory size.
    *
    * NB: This must be done last in initialization, when the rest of
    * the swapchain is valid.
    */
   if (swapchain_create_info->oldSwapchain != VK_NULL_HANDLE)
   {
      /* Set ancestor. */
      m_ancestor = swapchain_create_info->oldSwapchain;

      auto *ancestor = reinterpret_cast<swapchain_base *>(m_ancestor);
      ancestor->deprecate(reinterpret_cast<VkSwapchainKHR>(this));
   }

   m_is_valid = true;

   return VK_SUCCESS;
}

void swapchain_base::teardown()
{
   /* This method will block until all resources associated with this swapchain
    * are released. Images in the ACQUIRED or FREE state can be freed
    * immediately. For images in the PENDING state, we will block until the
    * presentation engine is finished with them. */

   int res;
   bool descendent_started_presenting = false;

   if (m_descendant != VK_NULL_HANDLE)
   {
      auto *desc = reinterpret_cast<swapchain_base *>(m_descendant);
      for (auto& img : desc->m_swapchain_images)
      {
         if (img.status == swapchain_image::PRESENTED ||
             img.status == swapchain_image::PENDING)
         {
            /* Here we wait for the start_present_semaphore, once this semaphore is up,
             * the descendant has finished waiting, we don't want to delete vkImages and vkFences
             * and semaphores before the waiting is done. */
            sem_wait(&desc->m_start_present_semaphore);

            descendent_started_presenting = true;
            break;
         }
      }
   }

   /* If descendant started presenting, there is no pending buffer in the swapchain. */
   if (m_is_valid && descendent_started_presenting == false)
   {
      wait_for_pending_buffers();
   }

   if (m_queue != VK_NULL_HANDLE)
   {
      /* Make sure the vkFences are done signaling. */
      m_device_data.disp.QueueWaitIdle(m_queue);
   }

   /* We are safe to destroy everything. */
   if (m_thread_sem_defined)
   {
      /* Tell flip thread to end. */
      m_page_flip_thread_run = false;

      if (m_page_flip_thread.joinable())
      {
         m_page_flip_thread.join();
      }
      else
      {
         WSI_LOG_ERROR("m_page_flip_thread is not joinable");
      }

      res = sem_destroy(&m_start_present_semaphore);
      if (res != 0)
      {
         WSI_LOG_ERROR("sem_destroy failed for start_present_semaphore with %d", errno);
      }
   }

   if (m_descendant != VK_NULL_HANDLE)
   {
      auto *sc = reinterpret_cast<swapchain_base *>(m_descendant);
      sc->clear_ancestor();
   }

   if (m_ancestor != VK_NULL_HANDLE)
   {
      auto *sc = reinterpret_cast<swapchain_base *>(m_ancestor);
      sc->clear_descendant();
   }
   /* Release the images array. */
   for (auto& img : m_swapchain_images)
   {
      /* Call implementation specific release */
      destroy_image(img);
   }
}

VkResult swapchain_base::acquire_next_image(uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *image_index)
{
   std::unique_lock<std::mutex> acquire_lock(m_image_acquire_lock);

   VkResult retval = wait_for_free_buffer(timeout);
   if (retval != VK_SUCCESS)
   {
      return retval;
   }

   if (!m_is_valid)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   uint32_t i;
   for (i = 0; i < m_swapchain_images.size(); ++i)
   {
      if (m_swapchain_images[i].status == swapchain_image::FREE)
      {
         m_swapchain_images[i].status = swapchain_image::ACQUIRED;
         *image_index = i;
         break;
      }
   }

   assert(i < m_swapchain_images.size());

   image_status_lock.unlock();

   if (VK_NULL_HANDLE != semaphore || VK_NULL_HANDLE != fence)
   {
      VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

      if (VK_NULL_HANDLE != semaphore)
      {
         submit.signalSemaphoreCount = 1;
         submit.pSignalSemaphores = &semaphore;
      }

      submit.commandBufferCount = 0;
      submit.pCommandBuffers = nullptr;
      retval = m_device_data.disp.QueueSubmit(m_queue, 1, &submit, fence);
      assert(retval == VK_SUCCESS);
   }

   return retval;
}

VkResult swapchain_base::get_swapchain_images(uint32_t *swapchain_image_count, VkImage *swapchain_images)
{
   if (swapchain_images == nullptr)
   {
      /* Return the number of swapchain images. */
      *swapchain_image_count = m_swapchain_images.size();

      return VK_SUCCESS;
   }
   else
   {
      assert(m_swapchain_images.size() > 0);
      assert(*swapchain_image_count > 0);

      /* Populate array, write actual number of images returned. */
      uint32_t current_image = 0;

      do
      {
         swapchain_images[current_image] = m_swapchain_images[current_image].image;

         current_image++;

         if (current_image == m_swapchain_images.size())
         {
            *swapchain_image_count = current_image;

            return VK_SUCCESS;
         }

      } while (current_image < *swapchain_image_count);

      /* If swapchain_image_count is smaller than the number of presentable images
       * in the swapchain, VK_INCOMPLETE must be returned instead of VK_SUCCESS. */
      *swapchain_image_count = current_image;

      return VK_INCOMPLETE;
   }
}

VkResult swapchain_base::queue_present(VkQueue queue, const VkPresentInfoKHR *present_info, uint32_t image_index)
{
   VkResult result;
   bool descendent_started_presenting = false;

   if (m_descendant != VK_NULL_HANDLE)
   {
      auto *desc = reinterpret_cast<swapchain_base *>(m_descendant);
      for (auto& img : desc->m_swapchain_images)
      {
         if (img.status == swapchain_image::PRESENTED ||
             img.status == swapchain_image::PENDING)
         {
            descendent_started_presenting = true;
            break;
         }
      }
   }

   result = image_set_present_payload(m_swapchain_images[image_index], queue, present_info->pWaitSemaphores,
                                      present_info->waitSemaphoreCount);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   const std::lock_guard<std::recursive_mutex> lock(m_image_status_mutex);
   /* If the descendant has started presenting, we should release the image. */
   if (descendent_started_presenting)
   {
      m_swapchain_images[image_index].status = swapchain_image::FREE;
      m_free_image_semaphore.post();
      return VK_ERROR_OUT_OF_DATE_KHR;
   }

   m_swapchain_images[image_index].status = swapchain_image::PENDING;
   m_pending_buffer_pool.push_back(image_index);
   m_page_flip_semaphore.post();
   return VK_SUCCESS;
}

void swapchain_base::deprecate(VkSwapchainKHR descendant)
{
   for (auto& img : m_swapchain_images)
   {
      if (img.status == swapchain_image::FREE)
      {
         destroy_image(img);
      }
   }

   /* Set its descendant. */
   m_descendant = descendant;
}

void swapchain_base::wait_for_pending_buffers()
{
   std::unique_lock<std::mutex> acquire_lock(m_image_acquire_lock);
   int wait;
   int acquired_images = 0;
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   for (auto& img : m_swapchain_images)
   {
      if (img.status == swapchain_image::ACQUIRED)
      {
         acquired_images++;
      }
   }

   /* Waiting for free images waits for both free and pending. One pending image may be presented and acquired by a
    * compositor. The WSI backend may not necessarily know which pending image is presented to change its state. It may
    * be impossible to wait for that one presented image. */
   wait = m_swapchain_images.size() - acquired_images - 1;
   image_status_lock.unlock();

   while (wait > 0)
   {
      /* Take down one free image semaphore. */
      wait_for_free_buffer(UINT64_MAX);
      --wait;
   }
}

void swapchain_base::clear_ancestor()
{
   m_ancestor = VK_NULL_HANDLE;
}

void swapchain_base::clear_descendant()
{
   m_descendant = VK_NULL_HANDLE;
}

VkResult swapchain_base::wait_for_free_buffer(uint64_t timeout)
{
   VkResult retval;
   /* first see if a buffer is already marked as free */
   retval = m_free_image_semaphore.wait(0);
   if (retval == VK_NOT_READY)
   {
      /* if not, we still have work to do even if timeout==0 -
       * the swapchain implementation may be able to get a buffer without
       * waiting */

      retval = get_free_buffer(&timeout);
      if (retval == VK_SUCCESS)
      {
         /* the sub-implementation has done it's thing, so re-check the
          * semaphore */
         retval = m_free_image_semaphore.wait(timeout);
      }
   }

   return retval;
}

} /* namespace wsi */
