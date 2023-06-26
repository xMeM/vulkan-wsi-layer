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
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

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

struct pending_completion
{
   uint32_t serial;
   uint64_t present_id;
};

struct x11_image_data
{
   /* Device memory backing the image. */
   VkDeviceMemory memory{};
   VkSubresourceLayout layout;

   fence_sync present_fence;

   xcb_pixmap_t pixmap;
   AHardwareBuffer *ahb;
   std::vector<pending_completion> pending_completions;
};

#define X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS 128

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator, surface *surface)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_connection(surface->get_connection())
   , m_window(surface->get_window())
   , m_surface(surface)
   , m_send_sbc(0)
   , m_target_msc(0)
   , m_last_present_msc(0)
   , m_thread_status_lock()
   , m_thread_status_cond()
{
}

swapchain::~swapchain()
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (m_present_event_thread_run)
   {
      m_present_event_thread_run = false;
      m_thread_status_cond.notify_all();
      thread_status_lock.unlock();

      if (m_present_event_thread.joinable())
      {
         m_present_event_thread.join();
      }

      thread_status_lock.lock();
   }

   xcb_unregister_for_special_event(m_connection, m_special_event);

   thread_status_lock.unlock();

   /* Call the base's teardown */
   teardown();
}

static uint32_t get_memory_type(VkPhysicalDeviceMemoryProperties2 memory_props, uint32_t req,
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

xcb_pixmap_t swapchain::create_pixmap(swapchain_image &image)
{
   auto data = reinterpret_cast<x11_image_data *>(image.data);

   int fds[] = { -1, -1 };
   if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
      return 0;

   auto pixmap = xcb_generate_id(m_connection);
   auto cookie = xcb_dri3_pixmap_from_buffers_checked(
      m_connection, pixmap, m_window, 1, m_image_create_info.extent.width, m_image_create_info.extent.height,
      data->layout.rowPitch, data->layout.offset, 0, 0, 0, 0, 0, 0, 24, 32, 1255, &fds[1]);
   xcb_flush(m_connection);

   uint8_t buf = 0;
   read(fds[0], &buf, 1);

   HardwareBuffer_sendHandleToUnixSocket(data->ahb, fds[0]);

   close(fds[0]);

   auto error = xcb_request_check(m_connection, cookie);
   if (error)
   {
      free(error);
      return false;
   }

   data->pixmap = pixmap;
   return true;
}

VkResult swapchain::allocate_and_bind_swapchain_image(VkImageCreateInfo image_create, swapchain_image &image)
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
   x11_image_data *data = nullptr;
   data = m_allocator.create<x11_image_data>(1);
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
      WSI_LOG_ERROR("vkBindImageMemory failed:%d", res);
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

   VkMemoryGetAndroidHardwareBufferInfoANDROID get_ahb_info;
   get_ahb_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
   get_ahb_info.pNext = nullptr;
   get_ahb_info.memory = data->memory;

   res = m_device_data.disp.GetMemoryAndroidHardwareBufferANDROID(m_device, &get_ahb_info, &data->ahb);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   if (!create_pixmap(image))
   {
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return VK_SUCCESS;
}

VkResult swapchain::create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image)
{
   m_image_create_info = image_create_info;
   return m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), &image.image);
}

void swapchain::present_event_thread()
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
   m_present_event_thread_run = true;

   while (m_present_event_thread_run)
   {
      auto assume_forward_progress = false;

      for (auto &image : m_swapchain_images)
      {
         if (image.status == swapchain_image::INVALID)
            continue;

         auto data = reinterpret_cast<x11_image_data *>(image.data);
         if (data->pending_completions.size() != 0)
         {
            assume_forward_progress = true;
            break;
         }
      }

      if (!assume_forward_progress)
      {
         m_thread_status_cond.wait(thread_status_lock);
         continue;
      }

      if (error_has_occured())
      {
         break;
      }

      thread_status_lock.unlock();

      auto event = xcb_wait_for_special_event(m_connection, m_special_event);
      if (event == nullptr)
      {
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         break;
      }

      thread_status_lock.lock();

      auto pe = reinterpret_cast<xcb_present_generic_event_t *>(event);
      switch (pe->evtype)
      {
      case XCB_PRESENT_EVENT_CONFIGURE_NOTIFY:
      {
         auto config = reinterpret_cast<xcb_present_configure_notify_event_t *>(event);
         if (config->pixmap_flags & (1 << 0))
         {
            set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         }
         else if (config->width != m_image_create_info.extent.width ||
                  config->height != m_image_create_info.extent.height)
         {
            set_error_state(VK_SUBOPTIMAL_KHR);
         }
         break;
      }
      case XCB_PRESENT_EVENT_IDLE_NOTIFY:
      {
         auto idle = reinterpret_cast<xcb_present_idle_notify_event_t *>(event);
         m_free_buffer_pool.push_back(idle->pixmap);
         m_thread_status_cond.notify_all();
         break;
      }
      case XCB_PRESENT_EVENT_COMPLETE_NOTIFY:
      {
         auto complete = reinterpret_cast<xcb_present_complete_notify_event_t *>(event);
         if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP)
         {
            for (auto &image : m_swapchain_images)
            {
               auto data = reinterpret_cast<x11_image_data *>(image.data);
               auto iter = std::find_if(data->pending_completions.begin(), data->pending_completions.end(),
                                        [complete](auto &pending_completion) -> bool {
                                           return complete->serial == pending_completion.serial;
                                        });
               if (iter != data->pending_completions.end())
               {
                  set_present_id(iter->present_id);
                  data->pending_completions.erase(iter);
                  m_thread_status_cond.notify_all();
               }
            }
            m_last_present_msc = complete->msc;
         }
         break;
      }
      }
      free(event);
   }

   m_present_event_thread_run = false;
   m_thread_status_cond.notify_all();
}

void swapchain::present_image(const pending_present_request &pending_present)
{
   auto image_data = reinterpret_cast<x11_image_data *>(m_swapchain_images[pending_present.image_index].data);
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   while (image_data->pending_completions.size() == X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS)
   {
      if (!m_present_event_thread_run)
      {
         set_present_id(pending_present.present_id);
         return unpresent_image(pending_present.image_index);
      }
      m_thread_status_cond.wait(thread_status_lock);
   }

   m_send_sbc++;
   uint32_t serial = (uint32_t)m_send_sbc;
   uint32_t options = XCB_PRESENT_OPTION_NONE;

   auto cookie = xcb_present_pixmap_checked(m_connection, m_window, image_data->pixmap, serial, 0, 0, 0, 0, 0, 0, 0,
                                            options, m_target_msc, 0, 0, 0, nullptr);
   xcb_discard_reply(m_connection, cookie.sequence);
   xcb_flush(m_connection);

   image_data->pending_completions.push_back({ serial, pending_present.present_id });
   m_thread_status_cond.notify_all();

   if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR)
   {
      while (image_data->pending_completions.size() > 0)
      {
         if (!m_present_event_thread_run)
         {
            return;
         }
         m_thread_status_cond.wait(thread_status_lock);
      }
      m_target_msc = m_last_present_msc + 1;
   }
}

bool swapchain::free_image_found()
{
   while (m_free_buffer_pool.size() > 0)
   {
      auto pixmap = m_free_buffer_pool.pop_front();
      assert(pixmap.has_value());
      for (int i = 0; i < m_swapchain_images.size(); i++)
      {
         auto data = reinterpret_cast<x11_image_data *>(m_swapchain_images[i].data);
         if (data->pixmap == pixmap.value())
         {
            unpresent_image(i);
         }
      }
   }

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
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (*timeout == 0)
   {
      return free_image_found() ? VK_SUCCESS : VK_NOT_READY;
   }
   else if (*timeout == UINT64_MAX)
   {
      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
            return VK_ERROR_OUT_OF_DATE_KHR;

         m_thread_status_cond.wait(thread_status_lock);
      }
   }
   else
   {
      auto time_point = std::chrono::high_resolution_clock::now() + std::chrono::nanoseconds(*timeout);

      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
            return VK_ERROR_OUT_OF_DATE_KHR;

         if (m_thread_status_cond.wait_until(thread_status_lock, time_point) == std::cv_status::timeout)
         {
            return VK_TIMEOUT;
         }
      }
   }

   *timeout = 0;
   return VK_SUCCESS;
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
      auto data = reinterpret_cast<x11_image_data *>(image.data);
      if (data->memory != VK_NULL_HANDLE)
      {
         m_device_data.disp.FreeMemory(m_device, data->memory, get_allocation_callbacks());
         data->memory = VK_NULL_HANDLE;
      }
      if (data->ahb)
      {
         HardwareBuffer_release(data->ahb);
         data->ahb = nullptr;
      }
      if (data->pixmap)
      {
         xcb_free_pixmap(m_connection, data->pixmap);
      }
      m_allocator.destroy(1, data);
      image.data = nullptr;
   }
}

VkResult swapchain::image_set_present_payload(swapchain_image &image, VkQueue queue,
                                              const queue_submit_semaphores &semaphores, const void *submission_pnext)
{
   auto data = reinterpret_cast<x11_image_data *>(image.data);
   return data->present_fence.set_payload(queue, semaphores, submission_pnext);
}

VkResult swapchain::image_wait_present(swapchain_image &image, uint64_t timeout)
{
   auto data = reinterpret_cast<x11_image_data *>(image.data);
   return data->present_fence.wait_payload(timeout);
}

VkResult swapchain::bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info)
{
   auto &device_data = layer::device_private_data::get(device);

   const wsi::swapchain_image &swapchain_image = m_swapchain_images[bind_sc_info->imageIndex];
   VkDeviceMemory memory = reinterpret_cast<x11_image_data *>(swapchain_image.data)->memory;

   return device_data.disp.BindImageMemory(device, bind_image_mem_info->image, memory, 0);
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   HardwareBuffer_release =
      reinterpret_cast<pfnAHardwareBuffer_release>(dlsym(RTLD_DEFAULT, "AHardwareBuffer_release"));
   HardwareBuffer_sendHandleToUnixSocket = reinterpret_cast<pfnAHardwareBuffer_sendHandleToUnixSocket>(
      dlsym(RTLD_DEFAULT, "AHardwareBuffer_sendHandleToUnixSocket"));
   m_device_data.instance_data.disp.GetPhysicalDeviceMemoryProperties2KHR(m_device_data.physical_device,
                                                                          &m_memory_props);
   if (m_surface == nullptr || HardwareBuffer_sendHandleToUnixSocket == nullptr || HardwareBuffer_release == nullptr)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto eid = xcb_generate_id(m_connection);
   m_special_event = xcb_register_for_special_xge(m_connection, &xcb_present_id, eid, nullptr);
   xcb_present_select_input(m_connection, eid, m_window,
                            XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY | XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                               XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY);

   try
   {
      m_present_event_thread = std::thread(&swapchain::present_event_thread, this);
   }
   catch (const std::system_error &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   catch (const std::bad_alloc &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /*
    * When VK_PRESENT_MODE_MAILBOX_KHR has been chosen by the application we don't
    * initialize the page flip thread so the present_image function can be called
    * during vkQueuePresent.
    */
   use_presentation_thread = (m_present_mode != VK_PRESENT_MODE_MAILBOX_KHR);

   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */
