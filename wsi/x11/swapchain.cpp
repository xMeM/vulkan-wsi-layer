/*
 * Copyright (c) 2017-2022 Arm Limited.
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
 * @brief Contains the implementation for a x11 swapchain.
 */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <util/timed_semaphore.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>

#include <xcb/present.h>
#include <xcb/dri3.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "swapchain.hpp"
#include "util/log.hpp"
#include "wsi/swapchain_base.hpp"

namespace wsi
{
namespace x11
{

typedef struct native_handle
{
   int version; /* sizeof(native_handle_t) */
   int numFds;  /* number of file-descriptors at &data[0] */
   int numInts; /* number of ints at &data[numFds] */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#endif
   int data[0]; /* numFds + numInts ints */
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
} native_handle_t;

struct external_memory_android
{
   external_memory_android(AHardwareBuffer *ahwb)
      : m_ahwb(ahwb)
   {
      m_libandroid_handle = dlopen("libandroid.so", RTLD_NOW);
      if (!m_libandroid_handle)
      {
         throw std::runtime_error(dlerror());
      }

#define LOAD_SYMBOL(name)                                                            \
   if ((pfn##name = (typeof pfn##name)dlsym(m_libandroid_handle, #name)) == nullptr) \
   {                                                                                 \
      throw std::runtime_error(dlerror());                                           \
   }

      LOAD_SYMBOL(AHardwareBuffer_release)
      LOAD_SYMBOL(AHardwareBuffer_describe)
      LOAD_SYMBOL(AHardwareBuffer_getNativeHandle)
      LOAD_SYMBOL(AHardwareBuffer_sendHandleToUnixSocket)
#undef LOAD_SYMBOL
   }

   ~external_memory_android()
   {
      pfnAHardwareBuffer_release(m_ahwb);
      dlclose(m_libandroid_handle);
   }

   int dupfd()
   {
      const native_handle_t *native_handle = pfnAHardwareBuffer_getNativeHandle(m_ahwb);
      if (native_handle)
      {
         AHardwareBuffer_Desc desc;
         pfnAHardwareBuffer_describe(m_ahwb, &desc);
         const int *handle_fds = &native_handle->data[0];
         const int num_fds = native_handle->numFds;

         for (int i = 0; i < num_fds; i++)
         {
            if (lseek(handle_fds[i], 0, SEEK_END) < desc.stride * desc.height * 4)
               continue;

            return fcntl(handle_fds[i], F_DUPFD_CLOEXEC, 0);
         }
      }

      return -1;
   }

   int send(int socket_fd)
   {
      return pfnAHardwareBuffer_sendHandleToUnixSocket(m_ahwb, socket_fd);
   }

private:
   void *m_libandroid_handle;
   AHardwareBuffer *m_ahwb;
   void (*pfnAHardwareBuffer_describe)(const AHardwareBuffer *buffer, AHardwareBuffer_Desc *outDesc);
   void (*pfnAHardwareBuffer_release)(AHardwareBuffer *buffer);
   const native_handle_t *(*pfnAHardwareBuffer_getNativeHandle)(const AHardwareBuffer *buffer);
   int (*pfnAHardwareBuffer_sendHandleToUnixSocket)(const AHardwareBuffer *buffer, int socketFd);
};

struct image_data
{
   /* Device memory backing the image. */
   VkDeviceMemory memory{};
   VkSubresourceLayout layout;
   void *map;

   xcb_pixmap_t pixmap;
   uint64_t serial;
   fence_sync present_fence;

   image_data()
      : map(nullptr)
      , pixmap(0)
      , serial(0)
   {
   }
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator, surface *surface)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_surface(surface)
   , m_send_sbc(0)
   , m_connection(surface->get_connection())
   , m_window(surface->get_window())
   , has_dri3(false)
   , has_present(false)
   , sw_wsi(false)
{
}

swapchain::~swapchain()
{
   auto cookie = xcb_free_gc(m_connection, m_gc);
   xcb_discard_reply(m_connection, cookie.sequence);

   /* Call the base's teardown */
   teardown();
}

static uint32_t get_memory_type(VkPhysicalDeviceMemoryProperties memory_props, uint32_t req,
                                VkMemoryPropertyFlagBits req_prop)
{
   size_t mem_type_idx = 0;
   for (; mem_type_idx < 8 * sizeof(req); ++mem_type_idx)
   {
      if (req & (1u << mem_type_idx) && req_prop & (1u << mem_type_idx))
      {
         return mem_type_idx;
      }

      if (req & (1u << mem_type_idx))
      {
         return mem_type_idx;
      }
   }

   return -1;
}

VkResult swapchain::create_and_bind_swapchain_image(VkImageCreateInfo image_create, wsi::swapchain_image &image)
{
   VkResult res = VK_SUCCESS;
   VkExternalMemoryHandleTypeFlags handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
   const std::lock_guard<std::recursive_mutex> lock(m_image_status_mutex);

   m_image_create_info = image_create;
   m_image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
   m_image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;

   VkExternalMemoryImageCreateInfo external_memory_image_create_info = {};
   external_memory_image_create_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
   external_memory_image_create_info.pNext = m_image_create_info.pNext;
   external_memory_image_create_info.handleTypes = handleType;
   m_image_create_info.pNext = &external_memory_image_create_info;

   res = m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), &image.image);
   if (res != VK_SUCCESS)
   {
      return res;
   }

   /* Find a memory type */
   size_t mem_type_idx = get_memory_type(m_memory_props, 0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   if (mem_type_idx < 0)
   {
      WSI_LOG_ERROR("required memory type not found");
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   VkMemoryDedicatedAllocateInfo memory_dedicated_allocate_info = {};
   memory_dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
   memory_dedicated_allocate_info.image = image.image;

   VkExportMemoryAllocateInfo export_memory_allocate_info = {};
   export_memory_allocate_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
   export_memory_allocate_info.pNext = &memory_dedicated_allocate_info;
   export_memory_allocate_info.handleTypes = handleType;

   VkMemoryAllocateInfo memory_allocate_info = {};
   memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   memory_allocate_info.pNext = &export_memory_allocate_info;
   memory_allocate_info.allocationSize = 0;
   memory_allocate_info.memoryTypeIndex = mem_type_idx;

   /* Create image_data */
   image_data *data = nullptr;
   data = m_allocator.create<image_data>(1);
   if (data == nullptr)
   {
      m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image.data = reinterpret_cast<void *>(data);
   image.status = wsi::swapchain_image::FREE;

   res = m_device_data.disp.AllocateMemory(m_device, &memory_allocate_info, get_allocation_callbacks(), &data->memory);
   if (res != VK_SUCCESS)
   {
      WSI_LOG_ERROR("vkAllocateMemory failed:%d", res);
      destroy_image(image);
      return res;
   }

   res = m_device_data.disp.BindImageMemory(m_device, image.image, data->memory, 0);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      WSI_LOG_ERROR("VkBindImageMemory failed:%d", res);
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

   VkImageSubresource subres = {};
   subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   m_device_data.disp.GetImageSubresourceLayout(m_device, image.image, &subres, &data->layout);

   if (!sw_wsi)
   {
      try
      {
         int fds[2];
         AHardwareBuffer *ahwb;

         VkMemoryGetAndroidHardwareBufferInfoANDROID get_ahb_info;
         get_ahb_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
         get_ahb_info.pNext = nullptr;
         get_ahb_info.memory = data->memory;

         res = m_device_data.disp.GetMemoryAndroidHardwareBufferANDROID(m_device, &get_ahb_info, &ahwb);
         if (res != VK_SUCCESS)
         {
            throw std::runtime_error("vkGetMemoryAndroidHardwareBufferANDROID failed");
         }
         auto external_memory = external_memory_android(ahwb);

         if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
         {
            throw std::runtime_error("socketpair failed");
         }

         data->pixmap = xcb_generate_id(m_connection);

         auto cookie = xcb_dri3_pixmap_from_buffers_checked(
            m_connection, data->pixmap, m_window, 1, m_image_create_info.extent.width,
            m_image_create_info.extent.height, data->layout.rowPitch, data->layout.offset, 0, 0, 0, 0, 0, 0, 24, 32,
            1255, &fds[1]);
         xcb_flush(m_connection);

         uint8_t buf = 0;
         read(fds[0], &buf, 1);

         external_memory.send(fds[0]);

         close(fds[0]);

         auto err = xcb_request_check(m_connection, cookie);
         if (err)
         {
            free(err);
            throw std::runtime_error("xcb_dri3_pixmap_from_buffers failed");
         }
      }
      catch (const std::exception &e)
      {
         WSI_LOG_ERROR("%s", e.what());
         WSI_LOG_ERROR("DRI3 is not available, falling back to sw WSI");
         sw_wsi = true;
      }
   }

   return VK_SUCCESS;
}

bool swapchain::free_image_found()
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   for (auto &img : m_swapchain_images)
   {
      if (img.status == swapchain_image::FREE)
      {
         image_status_lock.unlock();
         return true;
      }
   }

   image_status_lock.unlock();
   return false;
}

static long get_current_time_ns()
{
   std::timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_nsec;
}

VkResult swapchain::get_free_buffer(uint64_t *timeout)
{
   if (sw_wsi)
   {
      if (free_image_found())
         *timeout = 0;

      return VK_SUCCESS;
   }

   long time = get_current_time_ns();

   do
   {
      auto event = xcb_wait_for_special_event(m_connection, m_special_event);
      if (event == nullptr)
      {
         return VK_ERROR_SURFACE_LOST_KHR;
      }

      auto pe = reinterpret_cast<xcb_present_generic_event_t *>(event);
      switch (pe->evtype)
      {
      case XCB_PRESENT_EVENT_CONFIGURE_NOTIFY:
      {
         auto config = reinterpret_cast<xcb_present_configure_notify_event_t *>(event);
         if (config->pixmap_flags & (1 << 0))
            return VK_ERROR_SURFACE_LOST_KHR;
         else if (config->width != m_windowExtent.width || config->height != m_windowExtent.height)
            return VK_SUBOPTIMAL_KHR;

         break;
      }
      case XCB_PRESENT_EVENT_IDLE_NOTIFY:
      {
         auto idle = reinterpret_cast<xcb_present_idle_notify_event_t *>(event);
         for (auto i = 0; i < m_swapchain_images.size(); i++)
         {
            auto data = reinterpret_cast<image_data *>(m_swapchain_images[i].data);
            if (idle->pixmap == data->pixmap)
            {
               if (m_swapchain_images[i].status != swapchain_image::FREE)
               {
                  unpresent_image(i);
                  *timeout = 0;
                  return VK_SUCCESS;
               }
            }
         }
         break;
      }
      }
      free(event);
   } while (!free_image_found() && get_current_time_ns() - time < *timeout);

   if (*timeout == 0)
      return VK_NOT_READY;

   return VK_TIMEOUT;
}

void swapchain::present_image(uint32_t pending_index)
{
   image_data *image = reinterpret_cast<image_data *>(m_swapchain_images[pending_index].data);

   if (sw_wsi)
   {
      auto res = m_device_data.disp.MapMemory(m_device, image->memory, 0, VK_WHOLE_SIZE, 0, &image->map);
      if (res != VK_SUCCESS)
      {
         WSI_LOG_ERROR("vkMapMemory failed:%d", res);
         return;
      }
      int stride = image->layout.rowPitch;
      int bytesPerPixel = 4;
      int width = stride / bytesPerPixel;
      auto buffer = reinterpret_cast<uint8_t *>(image->map);
      size_t max_request_size = static_cast<size_t>(xcb_get_maximum_request_length(m_connection)) * 4;
      size_t max_strides = (max_request_size - sizeof(xcb_put_image_request_t)) / stride;
      for (size_t y = 0; y < m_windowExtent.height; y += max_strides)
      {
         size_t num_strides = std::min(max_strides, m_windowExtent.height - y);
         xcb_put_image(m_connection, XCB_IMAGE_FORMAT_Z_PIXMAP, m_window, m_gc, width, num_strides, 0, y, // dst x, y
                       0,                                                                                 // left_pad
                       m_depth,
                       num_strides * stride, // data_len
                       buffer + y * stride   // data
         );
      }
      m_device_data.disp.UnmapMemory(m_device, image->memory);
      unpresent_image(pending_index);
   }
   else
   {
      image->serial = ++m_send_sbc;
      xcb_present_pixmap(m_connection, m_window, image->pixmap, image->serial, 0, 0, 0, 0, 0, 0, 0,
                         XCB_PRESENT_OPTION_NONE, 0, 0, 0, 0, nullptr);
   }
   xcb_flush(m_connection);
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
      if (!sw_wsi)
      {
         xcb_free_pixmap(m_connection, data->pixmap);
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

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   if (m_surface == nullptr)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_device_data.instance_data.disp.GetPhysicalDeviceMemoryProperties(m_device_data.physical_device, &m_memory_props);

   m_surface->getWindowSizeAndDepth(&m_windowExtent, &m_depth);

   m_gc = xcb_generate_id(m_connection);
   auto gc_cookie = xcb_create_gc_checked(m_connection, m_gc, m_window, XCB_GC_GRAPHICS_EXPOSURES, (uint32_t[]){ 0 });
   xcb_request_check(m_connection, gc_cookie);

   auto dri3_cookie = xcb_dri3_query_version_unchecked(m_connection, 1, 2);
   auto dri3_reply = xcb_dri3_query_version_reply(m_connection, dri3_cookie, nullptr);
   has_dri3 = dri3_reply && (dri3_reply->major_version > 1 || dri3_reply->minor_version >= 2);
   free(dri3_reply);

   auto present_cookie = xcb_present_query_version_unchecked(m_connection, 1, 2);
   auto present_reply = xcb_present_query_version_reply(m_connection, present_cookie, nullptr);
   has_present = present_reply && (present_reply->major_version > 1 || present_reply->minor_version >= 2);
   free(present_reply);

   if (has_dri3 && has_present)
   {
      auto eid = xcb_generate_id(m_connection);
      m_special_event = xcb_register_for_special_xge(m_connection, &xcb_present_id, eid, nullptr);
      xcb_present_select_input(m_connection, eid, m_window,
                               XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY | XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                                  XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY);
   }

   use_presentation_thread = true;
   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */
