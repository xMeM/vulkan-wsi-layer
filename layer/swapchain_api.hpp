/*
 * Copyright (c) 2018-2019, 2024 Arm Limited.
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
 * @file swapchain_api.hpp
 *
 * @brief Contains the Vulkan entrypoints for the swapchain.
 */

#pragma once

#include <vulkan/vulkan.h>
#include "util/macros.hpp"

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo,
                               const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) VWL_API_POST;

VWL_VKAPI_CALL(void)
wsi_layer_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapc,
                                const VkAllocationCallbacks *pAllocator) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapc, uint32_t *pSwapchainImageCount,
                                  VkImage *pSwapchainImages) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapc, uint64_t timeout, VkSemaphore semaphore,
                                VkFence fence, uint32_t *pImageIndex) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) VWL_API_POST;

/* 1.1 entrypoints */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetDeviceGroupPresentCapabilitiesKHR(
   VkDevice device, VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
                                                 VkDeviceGroupPresentModeFlagsKHR *pModes) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                  uint32_t *pRectCount, VkRect2D *pRects) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
                                 uint32_t *pImageIndex) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                        VkImage *pImage) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                             const VkBindImageMemoryInfo *pBindInfos) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainStatusKHR(VkDevice device, VkSwapchainKHR swapchain) VWL_API_POST;
