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

#include <iostream>
#include <ostream>
#include <util/timed_semaphore.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>

#include <sys/socket.h>
#include <sys/mman.h>
#include <poll.h>
#include <xcb/present.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "swapchain.hpp"
#include "wsi/surface.hpp"

namespace wsi
{
namespace x11
{

struct image_data
{
   /* Device memory backing the image. */
   VkDeviceMemory memory{};
   VkSubresourceLayout layout;
   void *map;

   AHardwareBuffer *ahb = nullptr;
   xcb_shm_seg_t shmseg;
   xcb_pixmap_t pixmap;
   int dma_buf_fd = -1;
   fence_sync present_fence;
};

int HB_TO_DMABUF_FD(AHardwareBuffer *hb)
{
   int socks[2];
   if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == 0)
   {
      try
      {
         std::thread sender{ [&]
                             {
                                AHardwareBuffer_sendHandleToUnixSocket(hb, socks[1]);
                                close(socks[1]);
                             } };
         struct msghdr msg
         {
         };
         msg.msg_name = nullptr;
         msg.msg_namelen = 0;
         struct iovec iov
         {
         };
         char iobuf[100];
         iov.iov_base = iobuf;
         iov.iov_len = sizeof(iobuf);
         msg.msg_iov = &iov;
         msg.msg_iovlen = 1;
         constexpr int CONTROLLEN = CMSG_SPACE(sizeof(int) * 50);
         union
         {
            cmsghdr _; // for alignment
            char controlBuffer[CONTROLLEN];
         } controlBufferUnion;
         memset(&controlBufferUnion, 0, CONTROLLEN);
         msg.msg_control = &controlBufferUnion;
         msg.msg_controllen = sizeof(controlBufferUnion);
         const int fdindex = 0;
         int recfd = -1;
         errno = 0;
         while (recvmsg(socks[0], &msg, 0) > 0)
         {
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg))
            {
               if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS && recfd == -1)
               {
                  memcpy(&recfd, CMSG_DATA(cmsg) + sizeof(int) * fdindex, sizeof(recfd));
               }
            }
         }
         close(socks[0]);
         sender.join();
         return recfd;
      }
      catch (...)
      {
         close(socks[0]);
      }
   }
   return -1;
}

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator, surface *surface)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_surface(surface)
   , m_send_sbc(0)
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   , m_image_compression_control{}
#endif
{
}

swapchain::~swapchain()
{
   /* Call the base's teardown */
   auto cookie = xcb_free_gc(connection, gc);
   xcb_discard_reply(connection, cookie.sequence);
   teardown();
}

VkResult swapchain::create_and_bind_swapchain_image(VkImageCreateInfo image_create, wsi::swapchain_image &image)
{
   VkResult res = VK_SUCCESS;
   const std::lock_guard<std::recursive_mutex> lock(m_image_status_mutex);

   m_image_create_info = image_create;
   m_image_create_info.tiling = VK_IMAGE_TILING_LINEAR;

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   if (m_device_data.is_swapchain_compression_control_enabled())
   {
      /* Initialize compression control */
      m_image_compression_control.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
      m_image_compression_control.compressionControlPlaneCount =
         m_image_compression_control_params.compression_control_plane_count;
      m_image_compression_control.flags = m_image_compression_control_params.flags;
      m_image_compression_control.pFixedRateFlags = m_image_compression_control_params.fixed_rate_flags.data();
      m_image_compression_control.pNext = m_image_create_info.pNext;

      m_image_create_info.pNext = &m_image_compression_control;
   }
#endif
   res = m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), &image.image);
   if (res != VK_SUCCESS)
   {
      return res;
   }

   VkMemoryRequirements memory_requirements = {};
   m_device_data.disp.GetImageMemoryRequirements(m_device, image.image, &memory_requirements);

   /* Find a memory type */
   size_t mem_type_idx = 0;
   for (; mem_type_idx < 8 * sizeof(memory_requirements.memoryTypeBits); ++mem_type_idx)
   {
      if (memory_requirements.memoryTypeBits & (1u << mem_type_idx))
      {
         break;
      }
   }

   assert(mem_type_idx <= 8 * sizeof(memory_requirements.memoryTypeBits) - 1);

   VkMemoryAllocateInfo gmem_info = {};
   gmem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   gmem_info.allocationSize = memory_requirements.size;
   gmem_info.memoryTypeIndex = mem_type_idx;

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

   /* Alloc HardwareBuffer */
   uint32_t native_format;
   switch (m_image_create_info.format)
   {
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_UNORM:
      native_format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
      break;
   case VK_FORMAT_R8G8B8_UNORM:
      native_format = AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
      break;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      native_format = AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
      break;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      native_format = AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
      break;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      native_format = AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
      break;
   case VK_FORMAT_D16_UNORM:
      native_format = AHARDWAREBUFFER_FORMAT_D16_UNORM;
      break;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      native_format = AHARDWAREBUFFER_FORMAT_D24_UNORM;
      break;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      native_format = AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
      break;
   case VK_FORMAT_D32_SFLOAT:
      native_format = AHARDWAREBUFFER_FORMAT_D32_FLOAT;
      break;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      native_format = AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
      break;
   case VK_FORMAT_S8_UINT:
      native_format = AHARDWAREBUFFER_FORMAT_S8_UINT;
      break;
   case VK_FORMAT_R8_UNORM:
      native_format = AHARDWAREBUFFER_FORMAT_R8_UNORM;
      break;
   default:
      native_format = 0;
      break;
   }
   if (!native_format)
   {
      std::cout << "unsupported swapchain format=" << m_image_create_info.format << std::endl;
      m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   std::cout << "format: " << m_image_create_info.format << std::endl;
   AHardwareBuffer_Desc desc = {};
   desc.format = native_format;
   desc.layers = 1;
   desc.width = m_image_create_info.extent.width;
   desc.height = m_image_create_info.extent.height;
   desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
   if (AHardwareBuffer_allocate(&desc, &data->ahb) == 0)
   {
      VkImportAndroidHardwareBufferInfoANDROID hb_info = {};
      hb_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
      hb_info.buffer = data->ahb;
      hb_info.pNext = VK_NULL_HANDLE;
      gmem_info.pNext = &hb_info;
      dprintf(-1, ""); // fix fault
      std::cout << "HardwareBuffer alloc success." << std::endl;
   }
   else
   {
      std::cout << "HardwareBuffer alloc failed." << std::endl;
      destroy_image(image);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   res = m_device_data.disp.AllocateMemory(m_device, &gmem_info, get_allocation_callbacks(), &data->memory);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   res = m_device_data.disp.BindImageMemory(m_device, image.image, data->memory, 0);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
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

   VkImageSubresource subres = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .arrayLayer = 0,

   };
   m_device_data.disp.GetImageSubresourceLayout(m_device, image.image, &subres, &data->layout);

   if (has_shm && has_present)
   {
      data->dma_buf_fd = HB_TO_DMABUF_FD(data->ahb);
      if (data->dma_buf_fd != -1)
      {
         void *addr = mmap(nullptr, 4 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, data->dma_buf_fd, 0);
         if (addr != MAP_FAILED)
         {
            munmap(addr, 4 * 4);
         }
         else
         {
            has_shm = false;
         }
      }
   }

   if (has_shm)
   {
      auto conn = m_surface->connection;
      auto window = m_surface->window;
      data->shmseg = xcb_generate_id(conn);
      data->pixmap = xcb_generate_id(conn);
      auto cookie = xcb_shm_attach_fd_checked(conn, data->shmseg, data->dma_buf_fd, false);
      auto err = xcb_request_check(conn, cookie);
      if (err != nullptr)
      {
         std::cout << "attach shm fd failed." << std::endl;
         free(err);
      }
      cookie = xcb_shm_create_pixmap_checked(conn, data->pixmap, window, data->layout.rowPitch / 4,
                                             m_image_create_info.extent.height, 24, data->shmseg, 0);
      err = xcb_request_check(conn, cookie);
      if (err != nullptr)
      {
         free(err);
         has_shm = false;
         std::cout << "create shm pixmap failed." << std::endl;
      }
   }

   std::cout << "create swapchain image success." << std::endl;
   return res;
}

void swapchain::present_image(uint32_t pending_index)
{
   image_data *image = reinterpret_cast<image_data *>(m_swapchain_images[pending_index].data);

   if (has_shm && has_present)
   {
      auto serial = ++m_send_sbc;
      uint32_t options = XCB_PRESENT_OPTION_NONE;
      if (m_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR || m_present_mode == VK_PRESENT_MODE_MAILBOX_KHR ||
          m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
         options |= XCB_PRESENT_OPTION_ASYNC;
      auto cookie = xcb_present_pixmap_checked(connection, window, image->pixmap, serial, 0, 0, 0, 0, 0, 0, 0, options,
                                               0, 0, 0, 0, nullptr);
      auto err = xcb_request_check(connection, cookie);
      if (err != nullptr)
      {
         free(err);
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
      }
   }
   else
   {
      AHardwareBuffer_lock(image->ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, (void **)&image->map);
      int stride = image->layout.rowPitch;
      int bytesPerPixel = 4;
      int width = stride / bytesPerPixel;
      auto buffer = reinterpret_cast<uint8_t *>(image->map);
      size_t max_request_size = static_cast<size_t>(xcb_get_maximum_request_length(connection)) * 4;
      size_t max_strides = (max_request_size - sizeof(xcb_put_image_request_t)) / stride;
      for (size_t y = 0; y < windowExtent.height; y += max_strides)
      {
         size_t num_strides = std::min(max_strides, windowExtent.height - y);
         xcb_put_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, gc, width, num_strides, 0, y, // dst x, y
                       0,                                                                           // left_pad
                       depth,
                       num_strides * stride, // data_len
                       buffer + y * stride   // data
         );
      }
      int32_t fence = -1;
      AHardwareBuffer_unlock(image->ahb, &fence);
   }
   xcb_flush(connection);
   unpresent_image(pending_index);
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
      if (data->ahb != nullptr)
      {
         AHardwareBuffer_release(data->ahb);
      }
      if (has_shm)
      {
         xcb_free_pixmap(m_surface->connection, data->pixmap);
         xcb_shm_detach(m_surface->connection, data->shmseg);
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

   connection = m_surface->connection;
   window = m_surface->window;
   m_surface->getWindowSizeAndDepth(&windowExtent, &depth);

   gc = xcb_generate_id(connection);
   auto gc_cookie = xcb_create_gc_checked(connection, gc, window, XCB_GC_GRAPHICS_EXPOSURES, (uint32_t[]){ 0 });
   xcb_request_check(connection, gc_cookie);

   auto shm_cookie = xcb_shm_query_version_unchecked(connection);
   auto shm_reply = xcb_shm_query_version_reply(connection, shm_cookie, nullptr);
   if (shm_reply == nullptr ||
       (shm_reply->major_version != 1 || shm_reply->minor_version < 2 || shm_reply->shared_pixmaps == false))
   {
      free(shm_reply);
      has_shm = false;
      std::cout << "MIT-SHM disabled." << std::endl;
   }
   else
   {
      has_shm = true;
      std::cout << "MIT-SHM enabled." << std::endl;
   }
   if (has_shm)
   {
      auto present_cookie = xcb_present_query_version_unchecked(connection, 1, 2);
      auto present_reply = xcb_present_query_version_reply(connection, present_cookie, nullptr);
      if (present_reply == nullptr || present_reply->major_version != 1 || present_reply->minor_version < 2)
      {
         std::cout << "Present disabled." << std::endl;
      }
      else
      {
         std::cout << "Present enabled." << std::endl;
         has_present = true;
      }
   }

   // if (has_present)
   // {
   //    auto eid = xcb_generate_id(connection);
   //    special_event = xcb_register_for_special_xge(connection, &xcb_present_id, eid, nullptr);
   //    xcb_present_select_input(connection, eid, window,
   //                             XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY | XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
   //                                XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY);
   // }

   // switch (m_present_mode)
   // {
   // case VK_PRESENT_MODE_IMMEDIATE_KHR:
   //    std::cout << "present mode immediate." << std::endl;
   //    use_presentation_thread = false;
   //    break;
   // case VK_PRESENT_MODE_MAILBOX_KHR:
   //    std::cout << "present mode mailbox." << std::endl;
   //    use_presentation_thread = false;
   //    break;
   // default:
   //    std::cout << "present mode fifo." << std::endl;
   //    use_presentation_thread = true;
   //    break;
   // }

   use_presentation_thread = true;
   m_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;

   std::cout << "create swapchain." << std::endl;
   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */
