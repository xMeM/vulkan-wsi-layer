/*
 * Copyright (c) 2017-2019, 2021-2022 Arm Limited.
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
 * @file swapchain.hpp
 *
 * @brief Contains the class definition for a x11 swapchain.
 */

#pragma once

#include "surface.hpp"
#include <android/hardware_buffer.h>
#include <condition_variable>
#include <cstdint>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <wsi/swapchain_base.hpp>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/present.h>
#include <xcb/xproto.h>

namespace wsi
{
namespace x11
{

using pfnAHardwareBuffer_release = void (*)(AHardwareBuffer *);
using pfnAHardwareBuffer_sendHandleToUnixSocket = int (*)(AHardwareBuffer *, int);

/**
 * @brief x11 swapchain class.
 *
 * This class is mostly empty, because all the swapchain stuff is handled by the swapchain class,
 * which we inherit. This class only provides a way to create an image and page-flip ops.
 */
class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                      surface *wsi_surface);

   ~swapchain();

protected:
   /**
    * @brief Platform specific init
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                          bool &use_presentation_thread) override;
   /**
    * @brief Allocates and binds a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   VkResult allocate_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) override;

   /**
    * @brief Creates a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
    * depending on the error that occurred.
    */
   VkResult create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) override;

   /**
    * @brief Method to present and image
    *
    * It sends the next image for presentation to the presentation engine.
    *
    * @param pending_present Information on the pending present request.
    */
   void present_image(const pending_present_request &pending_present) override;

   /**
    * @brief Method to release a swapchain image
    *
    * @param image Handle to the image about to be released.
    */
   void destroy_image(wsi::swapchain_image &image) override;

   /**
    * @brief Sets the present payload for a swapchain image.
    *
    * @param[in] image       The swapchain image for which to set a present payload.
    * @param     queue       A Vulkan queue that can be used for any Vulkan commands needed.
    * @param[in] sem_payload Array of Vulkan semaphores that constitute the payload.
    * @param[in] submission_pnext Chain of pointers to attach to the payload submission.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult image_set_present_payload(swapchain_image &image, VkQueue queue, const queue_submit_semaphores &semaphores,
                                      const void *submission_pnext) override;

   VkResult image_wait_present(swapchain_image &image, uint64_t timeout) override;

   /**
    * @brief Bind image to a swapchain
    *
    * @param device              is the logical device that owns the images and memory.
    * @param bind_image_mem_info details the image we want to bind.
    * @param bind_sc_info        describes the swapchain memory to bind to.
    *
    * @return VK_SUCCESS on success, otherwise on failure VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_OUT_OF_DEVICE_MEMORY
    * can be returned.
    */
   VkResult bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                 const VkBindImageMemorySwapchainInfoKHR *bind_sc_info) override;

   /**
    * @brief Method to check if there are any free images
    *
    * @return true if any images are free, otherwise false.
    */
   bool free_image_found();

   /**
    * @brief Hook for any actions to free up a buffer for acquire
    *
    * @param[in,out] timeout time to wait, in nanoseconds. 0 doesn't block,
    *                        UINT64_MAX waits indefinitely. The timeout should
    *                        be updated if a sleep is required - this can
    *                        be set to 0 if the semaphore is now not expected
    *                        block.
    */
   VkResult get_free_buffer(uint64_t *timeout) override;

private:
   xcb_connection_t *m_connection;
   xcb_window_t m_window;

   surface *m_surface;
   uint64_t m_send_sbc;
   uint64_t m_target_msc;
   uint64_t m_last_present_msc;

   xcb_special_event_t *m_special_event;
   VkPhysicalDeviceMemoryProperties2 m_memory_props;

   xcb_pixmap_t create_pixmap(swapchain_image &image);

   void present_event_thread();
   bool m_present_event_thread_run;
   std::thread m_present_event_thread;
   std::mutex m_thread_status_lock;
   std::condition_variable m_thread_status_cond;
   util::ring_buffer<xcb_pixmap_t, 6> m_free_buffer_pool;

   pfnAHardwareBuffer_release HardwareBuffer_release;
   pfnAHardwareBuffer_sendHandleToUnixSocket HardwareBuffer_sendHandleToUnixSocket;
};

} /* namespace x11 */
} /* namespace wsi */
