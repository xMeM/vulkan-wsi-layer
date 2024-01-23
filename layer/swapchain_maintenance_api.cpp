/*
 * Copyright (c) 2024 Arm Limited.
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
 * @file swapchain_maintenance_api.cpp
 *
 * @brief Contains the Vulkan entrypoints for the swapchain maintenance.
 */

#include "swapchain_maintenance_api.hpp"
#include "private_data.hpp"

#include <wsi/wsi_factory.hpp>
#include <cassert>

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkReleaseSwapchainImagesEXT(VkDevice device, const VkReleaseSwapchainImagesInfoEXT *pReleaseInfo) VWL_API_POST
{
   if (pReleaseInfo == nullptr || pReleaseInfo->imageIndexCount == 0)
   {
      return VK_SUCCESS;
   }

   assert(pReleaseInfo->pImageIndices != nullptr);
   assert(pReleaseInfo->swapchain != VK_NULL_HANDLE);

   auto &device_data = layer::device_private_data::get(device);
   if (!device_data.layer_owns_swapchain(pReleaseInfo->swapchain))
   {
      return device_data.disp.ReleaseSwapchainImagesEXT(device, pReleaseInfo);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(pReleaseInfo->swapchain);
   sc->release_images(pReleaseInfo->imageIndexCount, pReleaseInfo->pImageIndices);

   return VK_SUCCESS;
}