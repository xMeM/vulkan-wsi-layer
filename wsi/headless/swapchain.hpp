/*
 * Copyright (c) 2017-2024 Arm Limited.
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
 * @brief Contains the class definition for a headless swapchain.
 */

#pragma once

#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <wsi/swapchain_base.hpp>

namespace wsi
{
namespace headless
{

/**
 * @brief Headless swapchain class.
 *
 * This class is mostly empty, because all the swapchain stuff is handled by the swapchain class,
 * which we inherit. This class only provides a way to create an image and page-flip ops.
 */
class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator);

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
   void destroy_image(wsi::swapchain_image &image);

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

private:
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   VkImageCompressionControlEXT m_image_compression_control;
#endif
};

} /* namespace headless */
} /* namespace wsi */
