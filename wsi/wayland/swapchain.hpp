/*
 * Copyright (c) 2017-2019, 2021 Arm Limited.
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

#include "wsi/swapchain_base.hpp"

extern "C" {
#include <vulkan/vk_icd.h>
#include <util/wsialloc/wsialloc.h>
#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
}

namespace wsi
{
namespace wayland
{

class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *allocator);

   ~swapchain();

   /* TODO: make the buffer destructor a friend? so this can be protected */
   void release_buffer(struct wl_buffer *wl_buffer);

protected:
   /**
    * @brief Initialize platform specifics.
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo) override;

   /**
    * @brief Creates a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    *
    * @param image Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
    * depending on the error that occurred.
    */
   VkResult create_image(const VkImageCreateInfo &image_create_info, swapchain_image &image) override;

   /**
    * @brief Method to present an image
    *
    * @param pendingIndex Index of the pending image to be presented.
    */
   void present_image(uint32_t pendingIndex) override;

   /**
    * @brief Method to release a swapchain image
    *
    * @param image Handle to the image about to be released.
    */
   void destroy_image(swapchain_image &image) override;

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
   struct wayland_image_data;
   VkResult allocate_image(const VkImageCreateInfo &image_create_info, wayland_image_data *image_data, VkImage *image);

   struct wl_display *m_display;
   struct wl_surface *m_surface;
   struct zwp_linux_dmabuf_v1 *m_dmabuf_interface;

   /* The queue on which we dispatch the swapchain related events, mostly frame completion */
   struct wl_event_queue *m_surface_queue;
   /* The queue on which we dispatch buffer related events, mostly buffer_release */
   struct wl_event_queue *m_buffer_queue;

   /**
    * @brief Handle to the WSI allocator.
    */
   wsialloc_allocator m_wsi_allocator;

   /**
    * @brief true when waiting for the server hint to present a buffer
    *
    * true if a buffer has been presented and we've not had a wl_surface::frame
    * callback to indicate the server is ready for the next buffer.
    */
   bool m_present_pending;
};
} // namespace wayland
} // namespace wsi
