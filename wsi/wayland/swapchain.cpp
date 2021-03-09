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

#define VK_USE_PLATFORM_WAYLAND_KHR 1

#include "swapchain.hpp"
#include "swapchain_wl_helpers.hpp"

#include <cstring>
#include <cassert>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <climits>

#include "util/drm/drm_utils.hpp"

#if VULKAN_WSI_DEBUG > 0
#define WSI_PRINT_ERROR(...) fprintf(stderr, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define WSI_PRINT_ERROR(...) (void)0
#endif

namespace wsi
{
namespace wayland
{

struct swapchain::wayland_image_data
{
   int buffer_fd;
   int stride;
   uint32_t offset;

   wl_buffer *buffer;
   VkDeviceMemory memory;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator)
   : swapchain_base(dev_data, pAllocator)
   , m_display(nullptr)
   , m_surface(nullptr)
   , m_dmabuf_interface(nullptr)
   , m_surface_queue(nullptr)
   , m_buffer_queue(nullptr)
   , m_wsi_allocator()
   , m_present_pending(false)
{
}

swapchain::~swapchain()
{
   int res;
   teardown();
   if (m_dmabuf_interface != nullptr)
   {
      zwp_linux_dmabuf_v1_destroy(m_dmabuf_interface);
   }
   res = wsialloc_delete(&m_wsi_allocator);
   if (res != 0)
   {
      WSI_PRINT_ERROR("error deleting the allocator: %d\n", res);
   }
   if (m_surface_queue != nullptr)
   {
      wl_event_queue_destroy(m_surface_queue);
   }
   if (m_buffer_queue != nullptr)
   {
      wl_event_queue_destroy(m_buffer_queue);
   }
}

struct display_queue
{
   wl_display *display;
   wl_event_queue *queue;
};

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo)
{
   VkIcdSurfaceWayland *vk_surf = reinterpret_cast<VkIcdSurfaceWayland *>(pSwapchainCreateInfo->surface);

   m_display = vk_surf->display;
   m_surface = vk_surf->surface;

   m_surface_queue = wl_display_create_queue(m_display);
   if (m_surface_queue == nullptr)
   {
      WSI_PRINT_ERROR("Failed to create wl surface display_queue.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_buffer_queue = wl_display_create_queue(m_display);
   if (m_buffer_queue == nullptr)
   {
      WSI_PRINT_ERROR("Failed to create wl buffer display_queue.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   wl_registry *registry = wl_display_get_registry(m_display);
   if (registry == nullptr)
   {
      WSI_PRINT_ERROR("Failed to get wl display registry.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   wl_proxy_set_queue((struct wl_proxy *)registry, m_surface_queue);

   const wl_registry_listener registry_listener = { registry_handler };
   int res = wl_registry_add_listener(registry, &registry_listener, &m_dmabuf_interface);
   if (res < 0)
   {
      WSI_PRINT_ERROR("Failed to add registry listener.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   res = wl_display_roundtrip_queue(m_display, m_surface_queue);
   if (res < 0)
   {
      WSI_PRINT_ERROR("Roundtrip failed.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* we should have the dma_buf interface by now */
   assert(m_dmabuf_interface);

   wl_registry_destroy(registry);

   res = wsialloc_new(-1, &m_wsi_allocator);
   if (res != 0)
   {
      WSI_PRINT_ERROR("Failed to create wsi allocator.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return VK_SUCCESS;
}

static void create_succeeded(void *data, struct zwp_linux_buffer_params_v1 *params, struct wl_buffer *buffer)
{
   struct wl_buffer **wayland_buffer = (struct wl_buffer **)data;
   *wayland_buffer = buffer;
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = { create_succeeded, NULL };

static void buffer_release(void *data, struct wl_buffer *wayl_buffer)
{
   swapchain *sc = (swapchain *)data;
   sc->release_buffer(wayl_buffer);
}

void swapchain::release_buffer(struct wl_buffer *wayl_buffer)
{
   uint32_t i;
   for (i = 0; i < m_swapchain_images.size(); i++)
   {
      wayland_image_data *data;
      data = (wayland_image_data *)m_swapchain_images[i].data;
      if (data->buffer == wayl_buffer)
      {
         unpresent_image(i);
         break;
      }
   }

   /* check we found a buffer to unpresent */
   assert(i < m_swapchain_images.size());
}

static struct wl_buffer_listener buffer_listener = { buffer_release };

VkResult swapchain::allocate_image(const VkImageCreateInfo &image_create_info, wayland_image_data *image_data,
                                   VkImage *image)
{
   VkResult result = VK_SUCCESS;

   image_data->buffer = nullptr;
   image_data->buffer_fd = -1;
   image_data->memory = VK_NULL_HANDLE;

   VkExternalImageFormatPropertiesKHR external_props = {};
   external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR;

   VkImageFormatProperties2KHR format_props = {};
   format_props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR;
   format_props.pNext = &external_props;
   {
      VkPhysicalDeviceExternalImageFormatInfoKHR external_info = {};
      external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO_KHR;
      external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      VkPhysicalDeviceImageDrmFormatModifierInfoEXT drm_mod_info = {};
      drm_mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
      drm_mod_info.pNext = &external_info;
      drm_mod_info.drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
      drm_mod_info.sharingMode = image_create_info.sharingMode;
      drm_mod_info.queueFamilyIndexCount = image_create_info.queueFamilyIndexCount;
      drm_mod_info.pQueueFamilyIndices = image_create_info.pQueueFamilyIndices;

      VkPhysicalDeviceImageFormatInfo2KHR info = {};
      info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR;
      info.pNext = &drm_mod_info;
      info.format = image_create_info.format;
      info.type = image_create_info.imageType;
      info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      info.usage = image_create_info.usage;
      info.flags = image_create_info.flags;

      result = m_device_data.instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(m_device_data.physical_device,
                                                                                           &info, &format_props);
   }
   if (result != VK_SUCCESS)
   {
      WSI_PRINT_ERROR("Failed to get physical device format support.\n");
      return result;
   }
   if (format_props.imageFormatProperties.maxExtent.width < image_create_info.extent.width ||
       format_props.imageFormatProperties.maxExtent.height < image_create_info.extent.height ||
       format_props.imageFormatProperties.maxExtent.depth < image_create_info.extent.depth)
   {
      WSI_PRINT_ERROR("Physical device does not support required extent.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   if (format_props.imageFormatProperties.maxMipLevels < image_create_info.mipLevels ||
       format_props.imageFormatProperties.maxArrayLayers < image_create_info.arrayLayers)
   {
      WSI_PRINT_ERROR("Physical device does not support required array layers or mip levels.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   if ((format_props.imageFormatProperties.sampleCounts & image_create_info.samples) != image_create_info.samples)
   {
      WSI_PRINT_ERROR("Physical device does not support required sample count.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (external_props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR)
   {
      /* TODO: Handle exportable images which use ICD allocated memory in preference to an external allocator. */
   }
   if (!(external_props.externalMemoryProperties.externalMemoryFeatures &
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT_KHR))
   {
      WSI_PRINT_ERROR("Export/Import not supported.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   else
   {
      /* TODO: Handle Dedicated allocation bit. */
      uint32_t fourcc = util::drm::vk_to_drm_format(image_create_info.format);

      int res =
         wsialloc_alloc(&m_wsi_allocator, fourcc, image_create_info.extent.width, image_create_info.extent.height,
                        &image_data->stride, &image_data->buffer_fd, &image_data->offset, nullptr);
      if (res != 0)
      {
         WSI_PRINT_ERROR("Failed allocation of DMA Buffer.\n");
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      {
         assert(image_data->stride >= 0);
         VkSubresourceLayout image_layout = {};
         image_layout.offset = image_data->offset;
         image_layout.rowPitch = static_cast<uint32_t>(image_data->stride);
         VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info = {};
         drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
         drm_mod_info.pNext = image_create_info.pNext;
         drm_mod_info.drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
         drm_mod_info.drmFormatModifierPlaneCount = 1;
         drm_mod_info.pPlaneLayouts = &image_layout;

         VkExternalMemoryImageCreateInfoKHR external_info = {};
         external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
         external_info.pNext = &drm_mod_info;
         external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

         VkImageCreateInfo image_info = image_create_info;
         image_info.pNext = &external_info;
         image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         result = m_device_data.disp.CreateImage(m_device, &image_info, get_allocation_callbacks(), image);
      }
      if (result != VK_SUCCESS)
      {
         WSI_PRINT_ERROR("Image creation failed.\n");
         return result;
      }
      {
         VkMemoryFdPropertiesKHR mem_props = {};
         mem_props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;

         result = m_device_data.disp.GetMemoryFdPropertiesKHR(m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                                              image_data->buffer_fd, &mem_props);
         if (result != VK_SUCCESS)
         {
            WSI_PRINT_ERROR("Error querying Fd properties.\n");
            return result;
         }

         uint32_t mem_idx;
         for (mem_idx = 0; mem_idx < VK_MAX_MEMORY_TYPES; mem_idx++)
         {
            if (mem_props.memoryTypeBits & (1 << mem_idx))
            {
               break;
            }
         }
         off_t dma_buf_size = lseek(image_data->buffer_fd, 0, SEEK_END);
         if (dma_buf_size < 0)
         {
            WSI_PRINT_ERROR("Failed to get DMA Buf size.\n");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

         VkImportMemoryFdInfoKHR import_mem_info = {};
         import_mem_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
         import_mem_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         import_mem_info.fd = image_data->buffer_fd;

         VkMemoryAllocateInfo alloc_info = {};
         alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
         alloc_info.pNext = &import_mem_info;
         alloc_info.allocationSize = static_cast<uint64_t>(dma_buf_size);
         alloc_info.memoryTypeIndex = mem_idx;

         result = m_device_data.disp.AllocateMemory(m_device, &alloc_info, get_allocation_callbacks(), &image_data->memory);
      }
      if (result != VK_SUCCESS)
      {
         WSI_PRINT_ERROR("Failed to import memory.\n");
         return result;
      }
      result = m_device_data.disp.BindImageMemory(m_device, *image, image_data->memory, 0);
   }

   return result;
}

VkResult swapchain::create_image(const VkImageCreateInfo &image_create_info, swapchain_image &image)
{
   uint32_t fourcc = util::drm::vk_to_drm_format(image_create_info.format);

   int res;
   VkResult result = VK_SUCCESS;

   wayland_image_data *image_data = nullptr;
   VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };

   /* Create image_data */
   if (get_allocation_callbacks() != nullptr)
   {
      image_data = static_cast<wayland_image_data *>(
         get_allocation_callbacks()->pfnAllocation(get_allocation_callbacks()->pUserData, sizeof(wayland_image_data),
                                                   alignof(wayland_image_data), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));
   }
   else
   {
      image_data = static_cast<wayland_image_data *>(malloc(sizeof(wayland_image_data)));
   }
   if (image_data == nullptr)
   {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto out;
   }

   image.data = reinterpret_cast<void *>(image_data);
   image.status = swapchain_image::FREE;
   result = allocate_image(image_create_info, image_data, &image.image);
   if (result != VK_SUCCESS)
   {
      WSI_PRINT_ERROR("Failed to allocate image.\n");
      goto out;
   }

   /* create a wl_buffer using the dma_buf protocol */
   struct zwp_linux_buffer_params_v1 *params;
   params = zwp_linux_dmabuf_v1_create_params(m_dmabuf_interface);
   zwp_linux_buffer_params_v1_add(params, image_data->buffer_fd, 0, image_data->offset, image_data->stride, 0, 0);
   wl_proxy_set_queue((struct wl_proxy *)params, m_surface_queue);
   res = zwp_linux_buffer_params_v1_add_listener(params, &params_listener, &image_data->buffer);
   if (res < 0)
   {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto out;
   }
   zwp_linux_buffer_params_v1_create(params, image_create_info.extent.width, image_create_info.extent.height, fourcc,
                                     0);

   /* TODO: don't roundtrip - we should be able to send the create request now,
    * and only wait for it on first present. only do this once, not for all buffers created */
   res = wl_display_roundtrip_queue(m_display, m_surface_queue);
   if (res < 0)
   {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto out;
   }

   /* should now have a wl_buffer */
   assert(image_data->buffer);
   zwp_linux_buffer_params_v1_destroy(params);
   wl_proxy_set_queue((struct wl_proxy *)image_data->buffer, m_buffer_queue);
   res = wl_buffer_add_listener(image_data->buffer, &buffer_listener, this);
   if (res < 0)
   {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto out;
   }

   /* Initialize presentation fence. */
   result = m_device_data.disp.CreateFence(m_device, &fenceInfo, get_allocation_callbacks(), &image.present_fence);

out:
   if (result != VK_SUCCESS)
   {
      destroy_image(image);
      return result;
   }
   return result;
}

static void frame_done(void *data, wl_callback *cb, uint32_t cb_data)
{
   (void)cb_data;

   bool *present_pending = reinterpret_cast<bool *>(data);
   assert(present_pending);

   *present_pending = false;

   wl_callback_destroy(cb);
}

void swapchain::present_image(uint32_t pendingIndex)
{
   int res;
   wayland_image_data *image_data = reinterpret_cast<wayland_image_data *>(m_swapchain_images[pendingIndex].data);
   /* if a frame is already pending, wait for a hint to present again */
   if (m_present_pending)
   {
      assert(m_present_mode == VK_PRESENT_MODE_FIFO_KHR);
      do
      {
         /* block waiting for the compositor to return the wl_surface::frame
          * callback. We may want to change this to timeout after a period of
          * time if the compositor isn't responding (perhaps because the
          * window is hidden).
          */
         res = dispatch_queue(m_display, m_surface_queue, -1);
      } while (res > 0 && m_present_pending);

      if (res <= 0)
      {
         WSI_PRINT_ERROR("error waiting for Wayland compositor frame hint\n");
         m_is_valid = false;
         /* try to present anyway */
      }
   }

   wl_surface_attach(m_surface, image_data->buffer, 0, 0);
   /* TODO: work out damage */
   wl_surface_damage(m_surface, 0, 0, INT32_MAX, INT32_MAX);

   if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR)
   {
      /* request a hint when we can present the _next_ frame */
      wl_callback *cb = wl_surface_frame(m_surface);
      if (cb)
      {
         wl_proxy_set_queue((wl_proxy *)cb, m_surface_queue);
         static const wl_callback_listener frame_listener = { frame_done };
         m_present_pending = true;
         wl_callback_add_listener(cb, &frame_listener, &m_present_pending);
      }
   }
   else
   {
      assert(m_present_mode == VK_PRESENT_MODE_MAILBOX_KHR);
      /* weston only _queues_ wl_buffer::release events. This means when the
       * compositor flushes the client it only sends the events if some other events
       * have been posted.
       *
       * As such we have to request a sync callback - we discard it straight away
       * as we don't actually need the callback, but it means the
       * wl_buffer::release event is actually sent.
       */
      wl_callback *cb = wl_display_sync(m_display);
      assert(cb);
      if (cb)
      {
         wl_callback_destroy(cb);
      }
   }

   wl_surface_commit(m_surface);
   res = wl_display_flush(m_display);
   if (res < 0)
   {
      WSI_PRINT_ERROR("error flushing the display\n");
      /* Setting the swapchain as invalid */
      m_is_valid = false;
   }
}

void swapchain::destroy_image(swapchain_image &image)
{
   if (image.status != swapchain_image::INVALID)
   {
      if (image.present_fence != VK_NULL_HANDLE)
      {
         m_device_data.disp.DestroyFence(m_device, image.present_fence, get_allocation_callbacks());
         image.present_fence = VK_NULL_HANDLE;
      }

      if (image.image != VK_NULL_HANDLE)
      {
         m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
         image.image = VK_NULL_HANDLE;
      }
   }
   if (image.data != nullptr)
   {
      auto image_data = reinterpret_cast<wayland_image_data *>(image.data);
      if (image_data->buffer != nullptr)
      {
         wl_buffer_destroy(image_data->buffer);
      }
      if (image_data->memory != VK_NULL_HANDLE)
      {
         m_device_data.disp.FreeMemory(m_device, image_data->memory, get_allocation_callbacks());
      }
      else if (image_data->buffer_fd >= 0)
      {
         close(image_data->buffer_fd);
      }

      if (get_allocation_callbacks() != nullptr)
      {
         get_allocation_callbacks()->pfnFree(get_allocation_callbacks()->pUserData, image_data);
      }
      else
      {
         free(image_data);
      }
      image.data = nullptr;
   }

   image.status = swapchain_image::INVALID;
}

bool swapchain::free_image_found()
{
   for (auto &img : m_swapchain_images)
   {
      if (img.status == swapchain_image::FREE)
      {
         return true;
      }
   }
   return false;
}

VkResult swapchain::get_free_buffer(uint64_t *timeout)
{
   int ms_timeout, res;

   if (*timeout >= INT_MAX * 1000llu * 1000llu)
   {
      ms_timeout = INT_MAX;
   }
   else
   {
      ms_timeout = *timeout / 1000llu / 1000llu;
   }

   /* The current dispatch_queue implementation will return if any
    * events are returned, even if no events are dispatched to the buffer
    * queue. Therefore dispatch repeatedly until a buffer has been freed.
    */
   do
   {
      res = dispatch_queue(m_display, m_buffer_queue, ms_timeout);
   } while (!free_image_found() && res > 0);

   if (res > 0)
   {
      *timeout = 0;
      return VK_SUCCESS;
   }
   else if (res == 0)
   {
      if (*timeout == 0)
      {
         return VK_NOT_READY;
      }
      else
      {
         return VK_TIMEOUT;
      }
   }
   else
   {
      return VK_ERROR_DEVICE_LOST;
   }
}

} // namespace wayland
} // namespace wsi
