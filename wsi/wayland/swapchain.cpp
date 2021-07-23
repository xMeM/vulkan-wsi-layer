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
#include "wl_helpers.hpp"
#include "surface_properties.hpp"

#include <stdint.h>
#include <cstring>
#include <cassert>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <climits>
#include <functional>

#include "util/drm/drm_utils.hpp"
#include "util/log.hpp"

#define MAX_PLANES 4

namespace wsi
{
namespace wayland
{

const VkImageAspectFlagBits plane_flag_bits[MAX_PLANES] = {
   VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT,
};

struct swapchain::wayland_image_data
{
   int buffer_fd[MAX_PLANES];
   int stride[MAX_PLANES];
   uint32_t offset[MAX_PLANES];

   wl_buffer *buffer;
   VkDeviceMemory memory[MAX_PLANES];

   uint32_t num_planes;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                     surface &wsi_surface)
   : swapchain_base(dev_data, pAllocator)
   , m_display(wsi_surface.get_wl_display())
   , m_surface(wsi_surface.get_wl_surface())
   , m_wsi_surface(&wsi_surface)
   , m_swapchain_queue(nullptr)
   , m_buffer_queue(nullptr)
   , m_wsi_allocator(nullptr)
   , m_present_pending(false)
{
}

swapchain::~swapchain()
{
   teardown();

   wsialloc_delete(m_wsi_allocator);
   m_wsi_allocator = nullptr;
   if (m_swapchain_queue != nullptr)
   {
      wl_event_queue_destroy(m_swapchain_queue);
   }
   if (m_buffer_queue != nullptr)
   {
      wl_event_queue_destroy(m_buffer_queue);
   }
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo)
{
   if ((m_display == nullptr) || (m_surface == nullptr) || (m_wsi_surface->get_dmabuf_interface() == nullptr))
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_swapchain_queue = wl_display_create_queue(m_display);

   if (m_swapchain_queue == nullptr)
   {
      WSI_LOG_ERROR("Failed to create swapchain wl queue.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_buffer_queue = wl_display_create_queue(m_display);
   if (m_buffer_queue == nullptr)
   {
      WSI_LOG_ERROR("Failed to create buffer wl queue.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (wsialloc_new(&m_wsi_allocator) != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed to create wsi allocator.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return VK_SUCCESS;
}

extern "C" void create_succeeded(void *data, struct zwp_linux_buffer_params_v1 *params,
                                 struct wl_buffer *buffer)
{
   auto wayland_buffer = reinterpret_cast<wl_buffer **>(data);
   *wayland_buffer = buffer;
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = { create_succeeded, NULL };

extern "C" void buffer_release(void *data, struct wl_buffer *wayl_buffer)
{
   auto sc = reinterpret_cast<swapchain *>(data);
   sc->release_buffer(wayl_buffer);
}

void swapchain::release_buffer(struct wl_buffer *wayl_buffer)
{
   uint32_t i;
   for (i = 0; i < m_swapchain_images.size(); i++)
   {
      auto data = reinterpret_cast<wayland_image_data *>(m_swapchain_images[i].data);
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

VkResult swapchain::allocate_plane_memory(int fd, VkDeviceMemory *memory)
{
   uint32_t mem_index = -1;
   VkResult result = get_fd_mem_type_index(fd, mem_index);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   const off_t dma_buf_size = lseek(fd, 0, SEEK_END);
   if (dma_buf_size < 0)
   {
      WSI_LOG_ERROR("Failed to get DMA Buf size.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   VkImportMemoryFdInfoKHR import_mem_info = {};
   import_mem_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
   import_mem_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
   import_mem_info.fd = fd;

   VkMemoryAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc_info.pNext = &import_mem_info;
   alloc_info.allocationSize = static_cast<uint64_t>(dma_buf_size);
   alloc_info.memoryTypeIndex = mem_index;

   result = m_device_data.disp.AllocateMemory(
      m_device, &alloc_info, get_allocation_callbacks(), memory);

   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to import memory.");
      return result;
   }

   return VK_SUCCESS;
}

VkResult swapchain::get_fd_mem_type_index(int fd, uint32_t &mem_idx)
{
   VkMemoryFdPropertiesKHR mem_props = {};
   mem_props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;

   VkResult result = m_device_data.disp.GetMemoryFdPropertiesKHR(
      m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &mem_props);
   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Error querying Fd properties.");
      return result;
   }

   for (mem_idx = 0; mem_idx < VK_MAX_MEMORY_TYPES; mem_idx++)
   {
      if (mem_props.memoryTypeBits & (1 << mem_idx))
      {
         break;
      }
   }

   assert(mem_idx < VK_MAX_MEMORY_TYPES);

   return VK_SUCCESS;
}

VkResult swapchain::get_drm_format_properties(
   VkFormat format, util::vector<VkDrmFormatModifierPropertiesEXT> &format_props_list)
{
   VkDrmFormatModifierPropertiesListEXT format_modifier_props = {};
   format_modifier_props.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

   VkFormatProperties2KHR format_props = {};
   format_props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR;
   format_props.pNext = &format_modifier_props;

   m_device_data.instance_data.disp.GetPhysicalDeviceFormatProperties2KHR(
      m_device_data.physical_device, format, &format_props);

   if (!format_props_list.try_resize(format_modifier_props.drmFormatModifierCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   format_modifier_props.pDrmFormatModifierProperties = format_props_list.data();
   m_device_data.instance_data.disp.GetPhysicalDeviceFormatProperties2KHR(
      m_device_data.physical_device, format, &format_props);

   return VK_SUCCESS;
}

static uint32_t get_same_fd_index(int fd, int const *fds)
{
   uint32_t index = 0;
   while (fd != fds[index])
   {
      index++;
   }

   return index;
}

VkResult swapchain::get_surface_compatible_formats(const VkImageCreateInfo &info,
                                                   util::vector<wsialloc_format> &importable_formats,
                                                   util::vector<uint64_t> &exportable_modifers)
{
   /* Query supported modifers. */
   util::vector<VkDrmFormatModifierPropertiesEXT> drm_format_props(
      util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   VkResult result = get_drm_format_properties(info.format, drm_format_props);
   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to get format properties.");
      return result;
   }
   for (const auto &prop : drm_format_props)
   {
      bool is_supported = false;
      drm_format_pair drm_format{ util::drm::vk_to_drm_format(info.format), prop.drmFormatModifier };
      for (const auto &format : m_wsi_surface->get_formats())
      {
         if (format.fourcc == drm_format.fourcc && format.modifier == drm_format.modifier)
         {
            is_supported = true;
            break;
         }
      }
      if (!is_supported)
      {
         continue;
      }

      VkExternalImageFormatPropertiesKHR external_props = {};
      external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR;

      VkImageFormatProperties2KHR format_props = {};
      format_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR;
      format_props.pNext = &external_props;
      {
         VkPhysicalDeviceExternalImageFormatInfoKHR external_info = {};
         external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO_KHR;
         external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

         VkPhysicalDeviceImageDrmFormatModifierInfoEXT drm_mod_info = {};
         drm_mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
         drm_mod_info.pNext = &external_info;
         drm_mod_info.drmFormatModifier = prop.drmFormatModifier;
         drm_mod_info.sharingMode = info.sharingMode;
         drm_mod_info.queueFamilyIndexCount = info.queueFamilyIndexCount;
         drm_mod_info.pQueueFamilyIndices = info.pQueueFamilyIndices;

         VkPhysicalDeviceImageFormatInfo2KHR image_info = {};
         image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR;
         image_info.pNext = &drm_mod_info;
         image_info.format = info.format;
         image_info.type = info.imageType;
         image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         image_info.usage = info.usage;
         image_info.flags = info.flags;

         result = m_device_data.instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(
            m_device_data.physical_device, &image_info, &format_props);
      }
      if (result != VK_SUCCESS)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxExtent.width < info.extent.width ||
          format_props.imageFormatProperties.maxExtent.height < info.extent.height ||
          format_props.imageFormatProperties.maxExtent.depth < info.extent.depth)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxMipLevels < info.mipLevels ||
          format_props.imageFormatProperties.maxArrayLayers < info.arrayLayers)
      {
         continue;
      }
      if ((format_props.imageFormatProperties.sampleCounts & info.samples) != info.samples)
      {
         continue;
      }

      if (external_props.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR)
      {
         if (!exportable_modifers.try_push_back(drm_format.modifier))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }

      if (external_props.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT_KHR)
      {
         uint64_t flags =
            (prop.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_DISJOINT_BIT) ? 0 : WSIALLOC_FORMAT_NON_DISJOINT;
         wsialloc_format import_format{ drm_format.fourcc, drm_format.modifier, flags };
         if (!importable_formats.try_push_back(import_format))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }
   }

   return VK_SUCCESS;
}

VkResult swapchain::allocate_image(VkImageCreateInfo &image_create_info, wayland_image_data *image_data, VkImage *image)
{
   image_data->buffer = nullptr;
   image_data->num_planes = 0;
   for (uint32_t plane = 0; plane < MAX_PLANES; plane++)
   {
      image_data->buffer_fd[plane] = -1;
      image_data->memory[plane] = VK_NULL_HANDLE;
   }

   util::vector<wsialloc_format> importable_formats(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   util::vector<uint64_t> exportable_modifiers(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   VkResult result = get_surface_compatible_formats(image_create_info, importable_formats, exportable_modifiers);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   /* TODO: Handle exportable images which use ICD allocated memory in preference to an external allocator. */

   if (importable_formats.empty())
   {
      WSI_LOG_ERROR("Export/Import not supported.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   else
   {
      /* TODO: Handle Dedicated allocation bit. */
      bool is_protected_memory = (image_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0;
      const uint64_t allocation_flags = is_protected_memory ? WSIALLOC_ALLOCATE_PROTECTED : 0;
      wsialloc_allocate_info alloc_info = { importable_formats.data(), static_cast<unsigned>(importable_formats.size()),
                                            image_create_info.extent.width, image_create_info.extent.height,
                                            allocation_flags };

      wsialloc_format allocated_format = { 0 };
      const auto res = wsialloc_alloc(m_wsi_allocator, &alloc_info, &allocated_format, image_data->stride,
                                      image_data->buffer_fd, image_data->offset);
      if (res != WSIALLOC_ERROR_NONE)
      {
         WSI_LOG_ERROR("Failed allocation of DMA Buffer. WSI error: %d", static_cast<int>(res));
         if (res == WSIALLOC_ERROR_NOT_SUPPORTED)
         {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
         }
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      bool is_disjoint = false;
      for (uint32_t plane = 0; plane < MAX_PLANES; plane++)
      {
         if (image_data->buffer_fd[plane] == -1)
         {
            break;
         }
         else if (image_data->buffer_fd[plane] != image_data->buffer_fd[0])
         {
            is_disjoint = true;
         }
         image_data->num_planes++;
      }

      {
         util::vector<VkSubresourceLayout> image_layout(
            util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
         if (!image_layout.try_resize(image_data->num_planes))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

         for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
         {
            assert(image_data->stride[plane] >= 0);
            image_layout[plane].offset = image_data->offset[plane];
            image_layout[plane].rowPitch = static_cast<uint32_t>(image_data->stride[plane]);
         }

         if (is_disjoint)
         {
            image_create_info.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
         }

         VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info = {};
         drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
         drm_mod_info.pNext = image_create_info.pNext;
         drm_mod_info.drmFormatModifier = allocated_format.modifier;
         drm_mod_info.drmFormatModifierPlaneCount = image_data->num_planes;
         drm_mod_info.pPlaneLayouts = image_layout.data();

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
         WSI_LOG_ERROR("Image creation failed.");
         return result;
      }
      {
         if (is_disjoint)
         {
            util::vector<VkBindImageMemoryInfo> bind_img_mem_infos(
               util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
            if (!bind_img_mem_infos.try_resize(image_data->num_planes))
            {
               return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            util::vector<VkBindImagePlaneMemoryInfo> bind_plane_mem_infos(
               util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
            if (!bind_plane_mem_infos.try_resize(image_data->num_planes))
            {
               return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
            {
               const auto fd_index = get_same_fd_index(image_data->buffer_fd[plane], image_data->buffer_fd);
               if (fd_index == plane)
               {
                  result = allocate_plane_memory(image_data->buffer_fd[plane], &image_data->memory[fd_index]);
                  if (result != VK_SUCCESS)
                  {
                     return result;
                  }
               }

               bind_plane_mem_infos[plane].planeAspect = plane_flag_bits[plane];
               bind_plane_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
               bind_plane_mem_infos[plane].pNext = NULL;

               bind_img_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
               bind_img_mem_infos[plane].pNext = &bind_plane_mem_infos[plane];
               bind_img_mem_infos[plane].image = *image;
               bind_img_mem_infos[plane].memory = image_data->memory[fd_index];
            }

            result =
               m_device_data.disp.BindImageMemory2KHR(m_device, bind_img_mem_infos.size(), bind_img_mem_infos.data());
         }
         else
         {
            result = allocate_plane_memory(image_data->buffer_fd[0], &image_data->memory[0]);
            if (result != VK_SUCCESS)
            {
               return result;
            }

            result = m_device_data.disp.BindImageMemory(m_device, *image, image_data->memory[0], 0);
         }
      }
   }

   return result;
}

VkResult swapchain::create_image(VkImageCreateInfo image_create_info, swapchain_image &image)
{
   /* Create image_data */
   auto image_data = m_allocator.create<wayland_image_data>(1);
   if (image_data == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   image.data = image_data;
   image.status = swapchain_image::FREE;
   VkResult result = allocate_image(image_create_info, image_data, &image.image);

   image_status_lock.unlock();

   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to allocate image.");
      destroy_image(image);
      return result;
   }

   /* create a wl_buffer using the dma_buf protocol */
   auto dmabuf_interface_proxy = make_proxy_with_queue(m_wsi_surface->get_dmabuf_interface(), m_swapchain_queue);
   if (dmabuf_interface_proxy == nullptr)
   {
      WSI_LOG_ERROR("Failed to allocate dma-buf interface proxy.");
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf_interface_proxy.get());

   for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
   {
      zwp_linux_buffer_params_v1_add(params, image_data->buffer_fd[plane], plane,
                                     image_data->offset[plane], image_data->stride[plane], 0, 0);
   }

   auto res = zwp_linux_buffer_params_v1_add_listener(params, &params_listener, &image_data->buffer);
   if (res < 0)
   {
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   const auto fourcc = util::drm::vk_to_drm_format(image_create_info.format);
   zwp_linux_buffer_params_v1_create(params, image_create_info.extent.width,
                                     image_create_info.extent.height, fourcc, 0);

   /* TODO: don't roundtrip - we should be able to send the create request now,
    * and only wait for it on first present. only do this once, not for all buffers created */
   res = wl_display_roundtrip_queue(m_display, m_swapchain_queue);
   if (res < 0)
   {
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* should now have a wl_buffer */
   assert(image_data->buffer);
   zwp_linux_buffer_params_v1_destroy(params);
   wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(image_data->buffer), m_buffer_queue);
   res = wl_buffer_add_listener(image_data->buffer, &buffer_listener, this);
   if (res < 0)
   {
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Initialize presentation fence. */
   VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
   result = m_device_data.disp.CreateFence(m_device, &fenceInfo, get_allocation_callbacks(), &image.present_fence);
   if (result != VK_SUCCESS)
   {
      destroy_image(image);
      return result;
   }

   return VK_SUCCESS;
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
         res = dispatch_queue(m_display, m_swapchain_queue, -1);
      } while (res > 0 && m_present_pending);

      if (res <= 0)
      {
         WSI_LOG_ERROR("error waiting for Wayland compositor frame hint");
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
      auto surface_proxy = make_proxy_with_queue(m_surface, m_swapchain_queue);
      if (surface_proxy == nullptr)
      {
         WSI_LOG_ERROR("failed to create wl_surface proxy");
         m_is_valid = false;
         return;
      }

      wl_callback *cb = wl_surface_frame(surface_proxy.get());
      if (cb != nullptr)
      {
         static const wl_callback_listener frame_listener = { frame_done };
         m_present_pending = true;
         wl_callback_add_listener(cb, &frame_listener, &m_present_pending);
      }
   }

   wl_surface_commit(m_surface);
   res = wl_display_flush(m_display);
   if (res < 0)
   {
      WSI_LOG_ERROR("error flushing the display");
      /* Setting the swapchain as invalid */
      m_is_valid = false;
   }
}

void swapchain::destroy_image(swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

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

      image.status = swapchain_image::INVALID;
   }

   image_status_lock.unlock();

   if (image.data != nullptr)
   {
      auto image_data = reinterpret_cast<wayland_image_data *>(image.data);
      if (image_data->buffer != nullptr)
      {
         wl_buffer_destroy(image_data->buffer);
      }

      for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
      {
         if (image_data->memory[plane] != VK_NULL_HANDLE)
         {
            m_device_data.disp.FreeMemory(m_device, image_data->memory[plane], get_allocation_callbacks());
         }
         else if (image_data->buffer_fd[plane] >= 0)
         {
            const auto same_fd_index = get_same_fd_index(image_data->buffer_fd[plane], image_data->buffer_fd);
            if (same_fd_index == plane)
            {
               close(image_data->buffer_fd[plane]);
            }
         }
      }

      m_allocator.destroy(1, image_data);
      image.data = nullptr;
   }
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
