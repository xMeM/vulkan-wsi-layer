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

#define VK_USE_PLATFORM_WAYLAND_KHR 1

#include "swapchain.hpp"
#include "wl_helpers.hpp"
#include "surface_properties.hpp"

#include <cstdint>
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
#include "util/helpers.hpp"
#include "util/macros.hpp"

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

   sync_fd_fence_sync present_fence;
   bool is_disjoint;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                     surface &wsi_surface)
   : swapchain_base(dev_data, pAllocator)
   , m_display(wsi_surface.get_wl_display())
   , m_surface(wsi_surface.get_wl_surface())
   , m_wsi_surface(&wsi_surface)
   , m_buffer_queue(nullptr)
   , m_wsi_allocator(nullptr)
   , m_image_creation_parameters({}, {}, m_allocator, {}, {})
{
   m_image_creation_parameters.m_image_create_info.format = VK_FORMAT_UNDEFINED;
}

swapchain::~swapchain()
{
   teardown();

   if (m_wsi_allocator != nullptr)
   {
      wsialloc_delete(m_wsi_allocator);
   }
   m_wsi_allocator = nullptr;
   if (m_buffer_queue != nullptr)
   {
      wl_event_queue_destroy(m_buffer_queue);
   }
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   if ((m_display == nullptr) || (m_surface == nullptr) || (m_wsi_surface->get_dmabuf_interface() == nullptr))
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_buffer_queue = wl_display_create_queue(m_display);
   if (m_buffer_queue == nullptr)
   {
      WSI_LOG_ERROR("Failed to create buffer wl queue.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   WSIALLOC_ASSERT_VERSION();
   if (wsialloc_new(&m_wsi_allocator) != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed to create wsi allocator.");
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

VWL_CAPI_CALL(void) buffer_release(void *data, struct wl_buffer *wayl_buffer) VWL_API_POST
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
      if (data && data->buffer == wayl_buffer)
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

VkResult swapchain::create_aliased_image_handle(const VkImageCreateInfo *image_create_info, VkImage *image)
{
   return m_device_data.disp.CreateImage(m_device, &m_image_creation_parameters.m_image_create_info,
                                         get_allocation_callbacks(), image);
}

VkResult swapchain::allocate_wsialloc(VkImageCreateInfo &image_create_info, wayland_image_data &image_data,
                                      util::vector<wsialloc_format> &importable_formats,
                                      wsialloc_format *allocated_format)
{
   bool is_protected_memory = (image_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0;
   const uint64_t allocation_flags = is_protected_memory ? WSIALLOC_ALLOCATE_PROTECTED : 0;
   wsialloc_allocate_info alloc_info = { importable_formats.data(), static_cast<unsigned>(importable_formats.size()),
                                       image_create_info.extent.width, image_create_info.extent.height,
                                       allocation_flags };
   const auto res = wsialloc_alloc(m_wsi_allocator, &alloc_info, allocated_format, image_data.stride,
                                   image_data.buffer_fd, image_data.offset);
   if (res != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed allocation of DMA Buffer. WSI error: %d", static_cast<int>(res));
      if (res == WSIALLOC_ERROR_NOT_SUPPORTED)
      {
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
      return VK_ERROR_OUT_OF_HOST_MEMORY;
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

   bool is_disjoint = false;
   util::vector<wsialloc_format> importable_formats(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   auto &m_image_create_info = m_image_creation_parameters.m_image_create_info;
   auto &m_allocated_format = m_image_creation_parameters.m_allocated_format;
   if (m_image_create_info.format != VK_FORMAT_UNDEFINED)
   {
      is_disjoint = (m_image_create_info.flags & VK_IMAGE_CREATE_DISJOINT_BIT) == VK_IMAGE_CREATE_DISJOINT_BIT;
      if (!importable_formats.try_push_back(m_allocated_format))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      VkResult result = allocate_wsialloc(m_image_create_info, *image_data, importable_formats, &m_allocated_format);
      if (result != VK_SUCCESS)
      {
         return result;
      }
      for (uint32_t plane = 0; plane < MAX_PLANES; plane++)
      {
         if (image_data->buffer_fd[plane] == -1)
         {
            break;
         }
         image_data->num_planes++;
      }
   }
   else
   {
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

      wsialloc_format allocated_format = { 0 };
      result = allocate_wsialloc(image_create_info, *image_data, importable_formats, &allocated_format);
      if (result != VK_SUCCESS)
      {
         return result;
      }

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

      auto &image_layout = m_image_creation_parameters.m_image_layout;
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

      auto &drm_mod_info = m_image_creation_parameters.m_drm_mod_info;

      drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
      drm_mod_info.pNext = image_create_info.pNext;
      drm_mod_info.drmFormatModifier = allocated_format.modifier;
      drm_mod_info.drmFormatModifierPlaneCount = image_data->num_planes;
      drm_mod_info.pPlaneLayouts = image_layout.data();

      auto &external_info = m_image_creation_parameters.m_external_info;
      external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
      external_info.pNext = &drm_mod_info;
      external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      m_image_create_info = image_create_info;
      m_image_create_info.pNext = &external_info;
      m_image_create_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

      m_allocated_format = allocated_format;
   }
   image_data->is_disjoint = is_disjoint;
   VkResult result = m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), image);
   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Image creation failed.");
      return result;
   }
   return result;
}

VkResult swapchain::create_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image)
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
   zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(m_wsi_surface->get_dmabuf_interface());
   uint32_t modifier_hi = m_image_creation_parameters.m_allocated_format.modifier >> 32;
   uint32_t modifier_low = m_image_creation_parameters.m_allocated_format.modifier & 0xFFFFFFFF;
   for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
   {
      zwp_linux_buffer_params_v1_add(params, image_data->buffer_fd[plane], plane,
                                     image_data->offset[plane], image_data->stride[plane], modifier_hi, modifier_low);
   }

   const auto fourcc = util::drm::vk_to_drm_format(image_create_info.format);
   image_data->buffer = zwp_linux_buffer_params_v1_create_immed(params, image_create_info.extent.width,
                                                                image_create_info.extent.height, fourcc, 0);
   zwp_linux_buffer_params_v1_destroy(params);

   wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(image_data->buffer), m_buffer_queue);
   auto res = wl_buffer_add_listener(image_data->buffer, &buffer_listener, this);
   if (res < 0)
   {
      destroy_image(image);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (image_data->is_disjoint)
   {
      for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
      {
         const auto fd_index = get_same_fd_index(image_data->buffer_fd[plane], image_data->buffer_fd);
         if (fd_index == plane)
         {
            VkResult result = allocate_plane_memory(image_data->buffer_fd[plane], &image_data->memory[fd_index]);
            if (result != VK_SUCCESS)
            {
               return result;
            }
         }
      }
   }
   else
   {
      VkResult result = allocate_plane_memory(image_data->buffer_fd[0], &image_data->memory[0]);
      if (result != VK_SUCCESS)
      {
         return result;
      }
   }

   result = internal_bind_swapchain_image(m_device, image_data, image.image);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   /* Initialize presentation fence. */
   auto present_fence = sync_fd_fence_sync::create(m_device_data);
   if (!present_fence.has_value())
   {
      destroy_image(image);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image_data->present_fence = std::move(present_fence.value());

   return VK_SUCCESS;
}

void swapchain::present_image(uint32_t pendingIndex)
{
   int res;
   wayland_image_data *image_data = reinterpret_cast<wayland_image_data *>(m_swapchain_images[pendingIndex].data);

   /* if a frame is already pending, wait for a hint to present again */
   if (!m_wsi_surface->wait_next_frame_event())
   {
      set_error_state(VK_ERROR_SURFACE_LOST_KHR);
   }

   wl_surface_attach(m_surface, image_data->buffer, 0, 0);

   auto present_sync_fd = image_data->present_fence.export_sync_fd();
   if (!present_sync_fd.has_value())
   {
      WSI_LOG_ERROR("Failed to export present fence.");
      set_error_state(VK_ERROR_SURFACE_LOST_KHR);
   }
   else if (present_sync_fd->is_valid())
   {
      zwp_linux_surface_synchronization_v1_set_acquire_fence(m_wsi_surface->get_surface_sync_interface(),
                                                             present_sync_fd->get());
   }

   /* TODO: work out damage */
   wl_surface_damage(m_surface, 0, 0, INT32_MAX, INT32_MAX);

   if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR)
   {
      if (!m_wsi_surface->set_frame_callback())
      {
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
      }
   }

   wl_surface_commit(m_surface);
   res = wl_display_flush(m_display);
   if (res < 0)
   {
      WSI_LOG_ERROR("error flushing the display");
      /* Setting the swapchain as invalid */
      set_error_state(VK_ERROR_SURFACE_LOST_KHR);
   }
}

void swapchain::destroy_image(swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);

   if (image.status != swapchain_image::INVALID)
   {
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

VkResult swapchain::image_set_present_payload(swapchain_image &image, VkQueue queue, const VkSemaphore *sem_payload,
                                              uint32_t sem_count)
{
   auto image_data = reinterpret_cast<wayland_image_data *>(image.data);
   return image_data->present_fence.set_payload(queue, sem_payload, sem_count);
}

VkResult swapchain::image_wait_present(swapchain_image &, uint64_t)
{
   /* With explicit sync in use there is no need to wait for the present sync before submiting the image to the
    * compositor. */
   return VK_SUCCESS;
}

VkResult swapchain::internal_bind_swapchain_image(VkDevice &device, wayland_image_data *image_data,
                                                  const VkImage &image)
{
   auto &device_data = layer::device_private_data::get(device);
   if (image_data->is_disjoint)
   {
      util::vector<VkBindImageMemoryInfo> bind_img_mem_infos(m_allocator);
      if (!bind_img_mem_infos.try_resize(image_data->num_planes))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      util::vector<VkBindImagePlaneMemoryInfo> bind_plane_mem_infos(m_allocator);
      if (!bind_plane_mem_infos.try_resize(image_data->num_planes))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      for (uint32_t plane = 0; plane < image_data->num_planes; plane++)
      {

         bind_plane_mem_infos[plane].planeAspect = plane_flag_bits[plane];
         bind_plane_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
         bind_plane_mem_infos[plane].pNext = NULL;

         bind_img_mem_infos[plane].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
         bind_img_mem_infos[plane].pNext = &bind_plane_mem_infos[plane];
         bind_img_mem_infos[plane].image = image;
         bind_img_mem_infos[plane].memory = image_data->memory[plane];
         bind_img_mem_infos[plane].memoryOffset = image_data->offset[plane];
      }

      return device_data.disp.BindImageMemory2KHR(device, bind_img_mem_infos.size(), bind_img_mem_infos.data());
   }

   return device_data.disp.BindImageMemory(device, image, image_data->memory[0], image_data->offset[0]);
}

VkResult swapchain::bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info)
{
   const wsi::swapchain_image &swapchain_image = m_swapchain_images[bind_sc_info->imageIndex];
   auto image_data = reinterpret_cast<wayland_image_data *>(swapchain_image.data);
   return internal_bind_swapchain_image(device, image_data, bind_image_mem_info->image);
}

} // namespace wayland
} // namespace wsi
