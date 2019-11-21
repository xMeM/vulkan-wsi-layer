/*
 * Copyright (c) 2016-2019 Arm Limited.
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

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vulkan/vk_layer.h>

#include "private_data.hpp"
#include "surface_api.hpp"
#include "swapchain_api.hpp"

#define VK_LAYER_API_VERSION VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION)

namespace layer
{

static const VkLayerProperties global_layer = {
   "VK_LAYER_window_system_integration",
   VK_LAYER_API_VERSION,
   1,
   "Window system integration layer",
};
static const VkExtensionProperties device_extension[] = { { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                                            VK_KHR_SWAPCHAIN_SPEC_VERSION } };
static const VkExtensionProperties instance_extension[] = { { VK_KHR_SURFACE_EXTENSION_NAME,
                                                              VK_KHR_SURFACE_SPEC_VERSION } };

VKAPI_ATTR VkResult extension_properties(const uint32_t count, const VkExtensionProperties *layer_ext, uint32_t *pCount,
                                         VkExtensionProperties *pProp)
{
   uint32_t size;

   if (pProp == NULL || layer_ext == NULL)
   {
      *pCount = count;
      return VK_SUCCESS;
   }

   size = *pCount < count ? *pCount : count;
   memcpy(pProp, layer_ext, size * sizeof(VkLayerProperties));
   *pCount = size;
   if (size < count)
   {
      return VK_INCOMPLETE;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult layer_properties(const uint32_t count, const VkLayerProperties *layer_prop, uint32_t *pCount,
                                     VkLayerProperties *pProp)
{
   uint32_t size;

   if (pProp == NULL || layer_prop == NULL)
   {
      *pCount = count;
      return VK_SUCCESS;
   }

   size = *pCount < count ? *pCount : count;
   memcpy(pProp, layer_prop, size * sizeof(VkLayerProperties));
   *pCount = size;
   if (size < count)
   {
      return VK_INCOMPLETE;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkLayerInstanceCreateInfo *get_chain_info(const VkInstanceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   VkLayerInstanceCreateInfo *chain_info = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = (VkLayerInstanceCreateInfo *)chain_info->pNext;
   }

   return chain_info;
}

VKAPI_ATTR VkLayerDeviceCreateInfo *get_chain_info(const VkDeviceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   VkLayerDeviceCreateInfo *chain_info = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = (VkLayerDeviceCreateInfo *)chain_info->pNext;
   }

   return chain_info;
}

/*
 * This is where we get our initialisation and construct our dispatch table. All layers must implement the function.
 * If you wish to intercept any device functions at all you need to implement vkCreateDevice.
 */
VKAPI_ATTR VkResult create_instance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                    VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *layerCreateInfo = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   PFN_vkSetInstanceLoaderData loader_callback =
      get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK)->u.pfnSetInstanceLoaderData;

   if (nullptr == layerCreateInfo || nullptr == layerCreateInfo->u.pLayerInfo)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Retrieve the vkGetInstanceProcAddr and the vkCreateInstance function pointers for the next layer in the chain. */
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(nullptr, "vkCreateInstance");
   if (nullptr == fpCreateInstance)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Advance the link info for the next element on the chain. */
   layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

   /* Now call create instance on the chain further down the list. */
   VkResult ret = fpCreateInstance(pCreateInfo, pAllocator, pInstance);

   instance_private_data::create(*pInstance, fpGetInstanceProcAddr, loader_callback);
   return ret;
}

VKAPI_ATTR VkResult create_device(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VkLayerDeviceCreateInfo *layerCreateInfo = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   PFN_vkSetDeviceLoaderData loader_callback =
      get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK)->u.pfnSetDeviceLoaderData;

   if (nullptr == layerCreateInfo || nullptr == layerCreateInfo->u.pLayerInfo)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Retrieve the vkGetDeviceProcAddr and the vkCreateDevice function pointers for the next layer in the chain. */
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
   if (nullptr == fpCreateDevice)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Advance the link info for the next element on the chain. */
   layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

   /* Now call create device on the chain further down the list. */
   VkResult ret = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

   device_private_data::create(*pDevice, fpGetDeviceProcAddr, physicalDevice, loader_callback);

   return ret;
}

} /* namespace layer */

extern "C"
{
   VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
   wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName);

   VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
   wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName);

   /* Clean up the dispatch table for this instance. */
   VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL
   wsi_layer_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
   {
      layer::instance_private_data::destroy(instance);
   }

   VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL
   wsi_layer_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
   {
      layer::device_private_data::destroy(device);
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   wsi_layer_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
   {
      return layer::create_instance(pCreateInfo, pAllocator, pInstance);
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   wsi_layer_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
   {
      return layer::create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
   {
      assert(pVersionStruct);
      assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

      /* 2 is the minimum interface version which would utilize this function. */
      assert(pVersionStruct->loaderLayerInterfaceVersion >= 2);

      /* Set our requested interface version. Set to 2 for now to separate us from newer versions. */
      pVersionStruct->loaderLayerInterfaceVersion = 2;

      /* Fill in struct values. */
      pVersionStruct->pfnGetInstanceProcAddr = &wsi_layer_vkGetInstanceProcAddr;
      pVersionStruct->pfnGetDeviceProcAddr = &wsi_layer_vkGetDeviceProcAddr;
      pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

      return VK_SUCCESS;
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   wsi_layer_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                  uint32_t *pCount, VkExtensionProperties *pProperties)
   {
      if (pLayerName && !strcmp(pLayerName, layer::global_layer.layerName))
         return layer::extension_properties(1, layer::device_extension, pCount, pProperties);

      assert(physicalDevice);
      return layer::instance_private_data::get(layer::get_key(physicalDevice))
         .disp.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   wsi_layer_vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount, VkExtensionProperties *pProperties)
   {
      if (pLayerName && !strcmp(pLayerName, layer::global_layer.layerName))
         return layer::extension_properties(1, layer::instance_extension, pCount, pProperties);

      return VK_ERROR_LAYER_NOT_PRESENT;
   }

   VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
   wsi_layer_vkEnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProperties)
   {
      return layer::layer_properties(1, &layer::global_layer, pCount, pProperties);
   }

   #define GET_PROC_ADDR(func)      \
   if (!strcmp(funcName, #func)) \
      return (PFN_vkVoidFunction)&wsi_layer_##func;

   VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
   wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName)
   {
      GET_PROC_ADDR(vkCreateSwapchainKHR);
      GET_PROC_ADDR(vkDestroySwapchainKHR);
      GET_PROC_ADDR(vkGetSwapchainImagesKHR);
      GET_PROC_ADDR(vkAcquireNextImageKHR);
      GET_PROC_ADDR(vkQueuePresentKHR);

      return layer::device_private_data::get(layer::get_key(device)).disp.GetDeviceProcAddr(device, funcName);
   }

   VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
   wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName)
   {
      GET_PROC_ADDR(vkGetDeviceProcAddr);
      GET_PROC_ADDR(vkGetInstanceProcAddr);
      GET_PROC_ADDR(vkCreateInstance);
      GET_PROC_ADDR(vkDestroyInstance);
      GET_PROC_ADDR(vkCreateDevice);
      GET_PROC_ADDR(vkDestroyDevice);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceSupportKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceFormatsKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfacePresentModesKHR);
      GET_PROC_ADDR(vkEnumerateDeviceExtensionProperties);
      GET_PROC_ADDR(vkEnumerateInstanceExtensionProperties);
      GET_PROC_ADDR(vkEnumerateInstanceLayerProperties);

      return layer::instance_private_data::get(layer::get_key(instance)).disp.GetInstanceProcAddr(instance, funcName);
   }
} /* extern "C" */
