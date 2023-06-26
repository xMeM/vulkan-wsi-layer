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

   surface *get_surface()
   {
      return m_surface;
   }

protected:
   /**
    * @brief Platform specific init
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                          bool &use_presentation_thread) override;
   /**
    * @brief Creates and binds a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
    * depending on the error that occured.
    */
   VkResult create_and_bind_swapchain_image(VkImageCreateInfo image_create_info, wsi::swapchain_image &image) override;

   /**
    * @brief Method to perform a present - just calls unpresent_image on x11
    *
    * @param pendingIndex Index of the pending image to be presented.
    *
    */
   void present_image(uint32_t pendingIndex) override;

   /**
    * @brief Method to release a swapchain image
    *
    * @param image Handle to the image about to be released.
    */
   void destroy_image(wsi::swapchain_image &image) override;

   VkResult image_set_present_payload(swapchain_image &image, VkQueue queue, const VkSemaphore *sem_payload,
                                      uint32_t sem_count) override;

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
   // bool free_image_found();
   //
   // VkResult get_free_buffer(uint64_t *timeout) override;
   //
   // VkResult poll_special_event(xcb_connection_t *c, xcb_special_event_t *se, uint64_t timeout);

private:
   surface *m_surface;
   uint64_t m_send_sbc;
   xcb_connection_t *connection;

   xcb_window_t window;
   xcb_gcontext_t gc;
   VkExtent2D windowExtent;
   int depth;

   bool has_shm = false;
   bool has_present = false;

   // xcb_special_event_t *special_event;
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   VkImageCompressionControlEXT m_image_compression_control;
#endif
};

} /* namespace x11 */
} /* namespace wsi */
