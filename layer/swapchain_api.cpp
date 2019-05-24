/*
 * Copyright (c) 2017, 2019 Arm Limited.
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
#include <vulkan/vk_icd.h>

#include <wsi/headless/swapchain.hpp>

#include "private_data.hpp"
#include "swapchain_api.hpp"

extern "C"
{

   VKAPI_ATTR VkResult wsi_layer_vkCreateSwapchainKHR(VkDevice device,
                                                      const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkSwapchainKHR *pSwapchain)
   {
      assert(pSwapchain != nullptr);

      wsi::swapchain_base *sc = nullptr;

      VkIcdSurfaceBase *surface_base = reinterpret_cast<VkIcdSurfaceBase *>(pSwapchainCreateInfo->surface);
      assert(VK_ICD_WSI_PLATFORM_HEADLESS == (int)surface_base->platform);

      void *memory = nullptr;
      if (pAllocator)
      {
         memory = static_cast<wsi::headless::swapchain *>(
            pAllocator->pfnAllocation(pAllocator->pUserData, sizeof(wsi::headless::swapchain),
                                      alignof(wsi::headless::swapchain), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE));
      }
      else
      {
         memory = static_cast<wsi::headless::swapchain *>(malloc(sizeof(wsi::headless::swapchain)));
      }

      if (memory)
      {
         sc = new (memory) wsi::headless::swapchain(layer::device_private_data::get(layer::get_key(device)), pAllocator);
      }

      if (sc == nullptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      VkResult result = sc->init(device, pSwapchainCreateInfo);
      if (result != VK_SUCCESS)
      {
         /* Error occured during initialization, need to free allocated memory. */
         sc->~swapchain_base();

         if (pAllocator != nullptr)
         {
            pAllocator->pfnFree(pAllocator->pUserData, reinterpret_cast<void *>(sc));
         }
         else
         {
            free(reinterpret_cast<void *>(sc));
         }

         return result;
      }

      *pSwapchain = reinterpret_cast<VkSwapchainKHR>(sc);

      return result;
   }

   VKAPI_ATTR void wsi_layer_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapc,
                                                   const VkAllocationCallbacks *pAllocator)
   {
      assert(swapc != VK_NULL_HANDLE);

      wsi::swapchain_base *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);

      sc->~swapchain_base();

      if (pAllocator != nullptr)
      {
         pAllocator->pfnFree(pAllocator->pUserData, reinterpret_cast<void *>(swapc));
      }
      else
      {
         free(reinterpret_cast<void *>(swapc));
      }
   }

   VKAPI_ATTR VkResult wsi_layer_vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapc,
                                                         uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
   {
      assert(pSwapchainImageCount != nullptr);
      assert(swapc != VK_NULL_HANDLE);

      wsi::swapchain_base *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);

      return sc->get_swapchain_images(pSwapchainImageCount, pSwapchainImages);
   }

   VKAPI_ATTR VkResult wsi_layer_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapc, uint64_t timeout,
                                                       VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
   {
      assert(swapc != VK_NULL_HANDLE);
      assert(semaphore != VK_NULL_HANDLE || fence != VK_NULL_HANDLE);
      assert(pImageIndex != nullptr);

      wsi::swapchain_base *sc = reinterpret_cast<wsi::swapchain_base *>(swapc);

      return sc->acquire_next_image(timeout, semaphore, fence, pImageIndex);
   }

   VKAPI_ATTR VkResult wsi_layer_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
   {
      assert(queue != VK_NULL_HANDLE);
      assert(pPresentInfo != nullptr);

      uint32_t resultMask = 0;

      for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
      {
         wsi::swapchain_base *sc = reinterpret_cast<wsi::swapchain_base *>(pPresentInfo->pSwapchains[i]);
         assert(sc != nullptr);

         VkResult res = sc->queue_present(queue, pPresentInfo, pPresentInfo->pImageIndices[i]);

         if (pPresentInfo->pResults != nullptr)
         {
            pPresentInfo->pResults[i] = res;
         }

         if (res == VK_ERROR_DEVICE_LOST)
            resultMask |= (1u << 1);
         else if (res == VK_ERROR_SURFACE_LOST_KHR)
            resultMask |= (1u << 2);
         else if (res == VK_ERROR_OUT_OF_DATE_KHR)
            resultMask |= (1u << 3);
      }

      if (resultMask & (1u << 1))
         return VK_ERROR_DEVICE_LOST;
      else if (resultMask & (1u << 2))
         return VK_ERROR_SURFACE_LOST_KHR;
      else if (resultMask & (1u << 3))
         return VK_ERROR_OUT_OF_DATE_KHR;

      return VK_SUCCESS;
   }

} /* extern "C" */
