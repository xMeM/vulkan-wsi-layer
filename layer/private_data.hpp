/*
 * Copyright (c) 2018-2019 Arm Limited.
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

#pragma once

#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"

#define DISPATCH_TABLE_ENTRY(x) PFN_vk##x x;

#define INSTANCE_ENTRYPOINTS_LIST(V)          \
   V(GetInstanceProcAddr)                     \
   V(GetPhysicalDeviceProperties)             \
   V(GetPhysicalDeviceImageFormatProperties)  \
   V(EnumerateDeviceExtensionProperties)      \
   V(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
   V(GetPhysicalDeviceSurfaceFormatsKHR)      \
   V(GetPhysicalDeviceSurfacePresentModesKHR) \
   V(GetPhysicalDeviceSurfaceSupportKHR)

namespace layer
{

template <typename DispatchableType>
inline void *get_key(DispatchableType dispatchable_object)
{
   return *(void **)dispatchable_object;
}

struct instance_dispatch_table
{
   instance_dispatch_table(VkInstance inst, PFN_vkGetInstanceProcAddr get_proc)
   {
#define DISPATCH_INIT(x) x = (PFN_vk##x)get_proc(inst, "vk" #x);
      INSTANCE_ENTRYPOINTS_LIST(DISPATCH_INIT);
#undef DISPATCH_INIT
   }

   INSTANCE_ENTRYPOINTS_LIST(DISPATCH_TABLE_ENTRY)
};

#define DEVICE_ENTRYPOINTS_LIST(V) \
   V(GetDeviceProcAddr)            \
   V(GetDeviceQueue)               \
   V(QueueSubmit)                  \
   V(QueueWaitIdle)                \
   V(CreateCommandPool)            \
   V(DestroyCommandPool)           \
   V(AllocateCommandBuffers)       \
   V(FreeCommandBuffers)           \
   V(ResetCommandBuffer)           \
   V(BeginCommandBuffer)           \
   V(EndCommandBuffer)             \
   V(CreateImage)                  \
   V(DestroyImage)                 \
   V(GetImageMemoryRequirements)   \
   V(BindImageMemory)              \
   V(AllocateMemory)               \
   V(FreeMemory)                   \
   V(CreateFence)                  \
   V(DestroyFence)                 \
   V(ResetFences)                  \
   V(WaitForFences)

struct device_dispatch_table
{
   device_dispatch_table(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc)
   {
#define DISPATCH_INIT(x) x = (PFN_vk##x)get_proc(dev, "vk" #x);
      DEVICE_ENTRYPOINTS_LIST(DISPATCH_INIT);
#undef DISPATCH_INIT
   }

   DEVICE_ENTRYPOINTS_LIST(DISPATCH_TABLE_ENTRY)
};

class instance_private_data
{
public:
   instance_private_data() = delete;
   static instance_private_data &create(VkInstance inst, PFN_vkGetInstanceProcAddr get_proc,
                                        PFN_vkSetInstanceLoaderData set_loader_data);
   static instance_private_data &get(void *key);
   static void destroy(VkInstance inst);

   instance_dispatch_table disp;
   PFN_vkSetInstanceLoaderData SetInstanceLoaderData;

private:
   instance_private_data(VkInstance inst, PFN_vkGetInstanceProcAddr get_proc,
                         PFN_vkSetInstanceLoaderData set_loader_data);
};

class device_private_data
{
public:
   device_private_data() = delete;
   static device_private_data &create(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc, VkPhysicalDevice phys_dev,
                                      PFN_vkSetDeviceLoaderData set_loader_data);
   static device_private_data &get(void *key);
   static void destroy(VkDevice dev);

   device_dispatch_table disp;
   instance_private_data &instance_data;
   PFN_vkSetDeviceLoaderData SetDeviceLoaderData;

private:
   device_private_data(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc, instance_private_data &inst_data,
                       PFN_vkSetDeviceLoaderData set_loader_data);
};

} /* namespace layer */
