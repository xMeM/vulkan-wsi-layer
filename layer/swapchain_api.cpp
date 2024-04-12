/*
 * Copyright (c) 2017, 2019, 2021-2024 Arm Limited.
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
 * @file swapchain_api.cpp
 *
 * @brief Contains the Vulkan entrypoints for the swapchain.
 */

#include <cassert>
#include <cstdlib>
#include <new>

#include <wsi/wsi_factory.hpp>

#include "private_data.hpp"
#include "swapchain_api.hpp"
#include <util/helpers.hpp>

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo,
                               const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) VWL_API_POST
{
   assert(pSwapchain != nullptr);
   layer::device_private_data &device_data = layer::device_private_data::get(device);
   VkSurfaceKHR surface = pSwapchainCreateInfo->surface;

   if (!device_data.should_layer_create_swapchain(surface))
   {
      if (!device_data.can_icds_create_swapchain(surface))
      {
         return VK_ERROR_INITIALIZATION_FAILED;
      }
      return device_data.disp.CreateSwapchainKHR(device_data.device, pSwapchainCreateInfo, pAllocator, pSwapchain);
   }

   auto sc = wsi::allocate_surface_swapchain(surface, device_data, pAllocator);
   if (sc == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG(sc->init(device, pSwapchainCreateInfo), "Failed to initialise swapchain");

   TRY_LOG(device_data.add_layer_swapchain(reinterpret_cast<VkSwapchainKHR>(sc.get())),
           "Failed to associate swapchain with the layer");

   *pSwapchain = reinterpret_cast<VkSwapchainKHR>(sc.release());
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapc,
                                const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   layer::device_private_data &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(swapc))
   {
      return device_data.disp.DestroySwapchainKHR(device_data.device, swapc, pAllocator);
   }

   assert(swapc != VK_NULL_HANDLE);
   device_data.remove_layer_swapchain(swapc);

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);
   wsi::destroy_surface_swapchain(sc, device_data, pAllocator);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapc, uint32_t *pSwapchainImageCount,
                                  VkImage *pSwapchainImages) VWL_API_POST
{
   layer::device_private_data &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(swapc))
   {
      return device_data.disp.GetSwapchainImagesKHR(device_data.device, swapc, pSwapchainImageCount, pSwapchainImages);
   }

   assert(pSwapchainImageCount != nullptr);
   assert(swapc != VK_NULL_HANDLE);
   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);
   return sc->get_swapchain_images(pSwapchainImageCount, pSwapchainImages);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapc, uint64_t timeout, VkSemaphore semaphore,
                                VkFence fence, uint32_t *pImageIndex) VWL_API_POST
{
   layer::device_private_data &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(swapc))
   {
      return device_data.disp.AcquireNextImageKHR(device_data.device, swapc, timeout, semaphore, fence, pImageIndex);
   }

   assert(swapc != VK_NULL_HANDLE);
   assert(semaphore != VK_NULL_HANDLE || fence != VK_NULL_HANDLE);
   assert(pImageIndex != nullptr);
   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);
   return sc->acquire_next_image(timeout, semaphore, fence, pImageIndex);
}

static VkResult submit_wait_request(VkQueue queue, const VkPresentInfoKHR &present_info,
                                    layer::device_private_data &device_data)
{
   util::vector<VkSemaphore> swapchain_semaphores{ util::allocator(device_data.get_allocator(),
                                                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) };
   if (!swapchain_semaphores.try_resize(present_info.swapchainCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < present_info.swapchainCount; ++i)
   {
      auto swapchain = reinterpret_cast<wsi::swapchain_base *>(present_info.pSwapchains[i]);
      swapchain_semaphores[i] = swapchain->get_image_present_semaphore(present_info.pImageIndices[i]);
   }

   util::vector<VkPipelineStageFlags> pipeline_stage_flags{ util::allocator(device_data.get_allocator(),
                                                                            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) };
   if (!pipeline_stage_flags.try_resize(present_info.waitSemaphoreCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < present_info.waitSemaphoreCount; ++i)
   {
      pipeline_stage_flags[i] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
   }

   VkSubmitInfo submit_info = {
      VK_STRUCTURE_TYPE_SUBMIT_INFO,
      NULL,
      present_info.waitSemaphoreCount,
      present_info.pWaitSemaphores,
      pipeline_stage_flags.data(),
      0,
      NULL,
      static_cast<uint32_t>(swapchain_semaphores.size()),
      swapchain_semaphores.data(),
   };

   TRY(device_data.disp.QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) VWL_API_POST
{
   assert(queue != VK_NULL_HANDLE);
   assert(pPresentInfo != nullptr);

   layer::device_private_data &device_data = layer::device_private_data::get(queue);

   if (!device_data.layer_owns_all_swapchains(pPresentInfo->pSwapchains, pPresentInfo->swapchainCount))
   {
      return device_data.disp.QueuePresentKHR(queue, pPresentInfo);
   }

   /* Avoid allocating on the heap when there is only one swapchain. */
   const VkPresentInfoKHR *present_info = pPresentInfo;
   if (pPresentInfo->swapchainCount > 1)
   {
      TRY_LOG_CALL(submit_wait_request(queue, *pPresentInfo, device_data));
      present_info = nullptr;
   }

   VkResult ret = VK_SUCCESS;
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
   {
      VkSwapchainKHR swapc = pPresentInfo->pSwapchains[i];

      auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);
      assert(sc != nullptr);

      VkResult res = sc->queue_present(queue, present_info, pPresentInfo->pImageIndices[i]);
      if (pPresentInfo->pResults != nullptr)
      {
         pPresentInfo->pResults[i] = res;
      }

      if (res != VK_SUCCESS && ret == VK_SUCCESS)
      {
         ret = res;
      }
   }

   return ret;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetDeviceGroupPresentCapabilitiesKHR(
   VkDevice device, VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities) VWL_API_POST
{
   assert(pDeviceGroupPresentCapabilities != nullptr);

   pDeviceGroupPresentCapabilities->presentMask[0] = 1;
   pDeviceGroupPresentCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   for (uint32_t i = 1; i < VK_MAX_DEVICE_GROUP_SIZE_KHR; i++)
   {
      pDeviceGroupPresentCapabilities->presentMask[i] = 0;
   }

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
                                                 VkDeviceGroupPresentModeFlagsKHR *pModes) VWL_API_POST
{
   assert(pModes != nullptr);

   auto &device_data = layer::device_private_data::get(device);
   auto &instance = device_data.instance_data;

   if (!instance.should_layer_handle_surface(device_data.physical_device, surface))
   {
      return device_data.disp.GetDeviceGroupSurfacePresentModesKHR(device, surface, pModes);
   }

   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                  uint32_t *pRectCount, VkRect2D *pRects) VWL_API_POST
{
   assert(surface);
   assert(pRectCount != nullptr);

   auto &instance = layer::instance_private_data::get(physicalDevice);

   if (!instance.should_layer_handle_surface(physicalDevice, surface))
   {
      return instance.disp.GetPhysicalDevicePresentRectanglesKHR(physicalDevice, surface, pRectCount, pRects);
   }

   VkResult result;
   wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
   assert(props);

   if (nullptr == pRects)
   {
      *pRectCount = 1;
      result = VK_SUCCESS;
   }
   else if (0 == *pRectCount)
   {
      result = VK_INCOMPLETE;
   }
   else
   {
      *pRectCount = 1;

      VkSurfaceCapabilitiesKHR surface_caps = {};
      result = props->get_surface_capabilities(physicalDevice, &surface_caps);

      if (result != VK_SUCCESS)
      {
         return result;
      }

      pRects[0].offset.x = 0;
      pRects[0].offset.y = 0;
      pRects[0].extent = surface_caps.currentExtent;
   }

   return result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
                                 uint32_t *pImageIndex) VWL_API_POST
{
   assert(pAcquireInfo != VK_NULL_HANDLE);
   assert(pAcquireInfo->swapchain != VK_NULL_HANDLE);
   assert(pAcquireInfo->semaphore != VK_NULL_HANDLE || pAcquireInfo->fence != VK_NULL_HANDLE);
   assert(pImageIndex != nullptr);

   auto &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(pAcquireInfo->swapchain))
   {
      return device_data.disp.AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(pAcquireInfo->swapchain);

   return sc->acquire_next_image(pAcquireInfo->timeout, pAcquireInfo->semaphore, pAcquireInfo->fence, pImageIndex);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                        VkImage *pImage) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);

   const auto *image_sc_create_info = util::find_extension<VkImageSwapchainCreateInfoKHR>(
      VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR, pCreateInfo->pNext);

   if (image_sc_create_info == nullptr || !device_data.layer_owns_swapchain(image_sc_create_info->swapchain))
   {
      return device_data.disp.CreateImage(device_data.device, pCreateInfo, pAllocator, pImage);
   }

   auto sc = reinterpret_cast<wsi::swapchain_base *>(image_sc_create_info->swapchain);
   return sc->create_aliased_image_handle(pImage);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                             const VkBindImageMemoryInfo *pBindInfos) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);

   VkResult endpoint_result = VK_SUCCESS;
   bool maintenance_6 = device_data.is_device_extension_enabled(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
   for (uint32_t i = 0; i < bindInfoCount; i++)
   {
      VkResult result = VK_SUCCESS;
      std::string_view error_message{};

      const auto *bind_sc_info = util::find_extension<VkBindImageMemorySwapchainInfoKHR>(
         VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR, pBindInfos[i].pNext);

      if (bind_sc_info == nullptr || bind_sc_info->swapchain == VK_NULL_HANDLE ||
          !device_data.layer_owns_swapchain(bind_sc_info->swapchain))
      {
         result = device_data.disp.BindImageMemory2KHR(device, 1, &pBindInfos[i]);
         error_message = "Failed to bind image memory";
      }
      else
      {
         auto sc = reinterpret_cast<wsi::swapchain_base *>(bind_sc_info->swapchain);
         result = sc->bind_swapchain_image(device, &pBindInfos[i], bind_sc_info);
         error_message = "Failed to bind an image to the swapchain";
      }

      if (maintenance_6)
      {
         const auto *bind_status =
            util::find_extension<VkBindMemoryStatusKHR>(VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR, pBindInfos[i].pNext);
         if (nullptr != bind_status)
         {
            assert(bind_status->pResult != nullptr);
            *bind_status->pResult = result;
         }
      }

      if (VK_SUCCESS != result)
      {
         /* VK_KHR_maintenance6 requires that all memory binding operations must be attempted, so the results are stored
         rather than returned early upon failure */
         WSI_LOG_ERROR("%s", error_message.data());
         endpoint_result = result;
      }
   }
   return endpoint_result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainStatusKHR(VkDevice device, VkSwapchainKHR swapchain) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(swapchain))
   {
      return device_data.disp.GetSwapchainStatusKHR(device, swapchain);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapchain);

   return sc->get_swapchain_status();
}
