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

#pragma once

#include "wsi/swapchain_base.hpp"
#include "wl_helpers.hpp"

extern "C" {
#include <vulkan/vk_icd.h>
}

#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include "util/wsialloc/wsialloc.h"
#include "util/custom_allocator.hpp"
#include "wl_object_owner.hpp"
#include "surface.hpp"

namespace wsi
{
namespace wayland
{

struct image_creation_parameters
{
   wsialloc_format m_allocated_format;
   util::vector<VkSubresourceLayout> m_image_layout;
   VkExternalMemoryImageCreateInfoKHR m_external_info;
   VkImageDrmFormatModifierExplicitCreateInfoEXT m_drm_mod_info;

   image_creation_parameters(wsialloc_format allocated_format,
                             util::allocator allocator, VkExternalMemoryImageCreateInfoKHR external_info,
                             VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info)
      : m_allocated_format(allocated_format)
      , m_image_layout(allocator)
      , m_external_info(external_info)
      , m_drm_mod_info(drm_mod_info)
   {
   }
};

class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *allocator,
                      surface &wsi_surface);

   ~swapchain();

   /* TODO: make the buffer destructor a friend? so this can be protected */
   void release_buffer(struct wl_buffer *wl_buffer);

protected:
   /**
    * @brief Initialize platform specifics.
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
    * depending on the error that occurred.
    */
   VkResult create_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) override;

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

private:
   struct wayland_image_data;

   VkResult allocate_image(VkImageCreateInfo &image_create_info, wayland_image_data *image_data, VkImage *image);
   VkResult allocate_wsialloc(VkImageCreateInfo &image_create_info, wayland_image_data &image_data,
                              util::vector<wsialloc_format> &importable_formats, wsialloc_format *allocated_format);
   VkResult internal_bind_swapchain_image(VkDevice &device, wayland_image_data *swapchain_image,
                                          const VkImage &image);

   struct wl_display *m_display;
   struct wl_surface *m_surface;
   /** Raw pointer to the WSI Surface that this swapchain was created from. The Vulkan specification ensures that the
    * surface is valid until swapchain is destroyed. */
   surface *m_wsi_surface;

   /* The queue on which we dispatch buffer related events, mostly buffer_release */
   struct wl_event_queue *m_buffer_queue;

   /**
    * @brief Handle to the WSI allocator.
    */
   wsialloc_allocator *m_wsi_allocator;

   /**
    * @brief Image creation parameters used for all swapchain images.
    */
   struct image_creation_parameters m_image_creation_parameters;

   /*
    * @brief Allocate memory for an image plane.
    *
    * Allocates a VkDeviceMemory object from a given fd for an image plane. First
    * it makes a call to get_fd_mem_type_index() to acquire the memory type for
    * the given fd and then it allocates device memory by calling vkAllocateMemory().
    *
    * @param      fd     The plane's fd.
    * @param[out] memory The allocated VkDeviceMemory object.
    *
    * @return VK_SUCCESS on success. If one of the functions that are being called
    * fails its return value is returned. VK_ERROR_OUT_OF_HOST_MEMORY is returned
    * when the host gets out of memory.
    */
   VkResult allocate_plane_memory(int fd, VkDeviceMemory *memory);

   /*
    * @brief Get the memory type which the specified file descriptor can be
    * imported as.
    *
    * @param      fd      The given fd.
    * @param[out] mem_idx The index of the supported memory type.
    *
    * @return VK_SUCCESS on success. On failure the error value of
    * vkGetMemoryFdPropertiesKHR is returned.
    */
   VkResult get_fd_mem_type_index(int fd, uint32_t &mem_idx);

   /**
    * @brief Finds what formats are compatible with the requested swapchain image Vulkan Device and Wayland surface.
    *
    * @param      info               The Swapchain image creation info.
    * @param[out] importable_formats A list of formats that can be imported to the Vulkan Device.
    * @param[out] exportable_formats A list of formats that can be exported from the Vulkan Device.
    *
    * @return VK_SUCCESS or VK_ERROR_OUT_OF_HOST_MEMORY
    */
   VkResult get_surface_compatible_formats(const VkImageCreateInfo &info,
                                           util::vector<wsialloc_format> &importable_formats,
                                           util::vector<uint64_t> &exportable_modifers);

};
} // namespace wayland
} // namespace wsi
