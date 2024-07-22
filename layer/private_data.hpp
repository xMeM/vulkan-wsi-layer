/*
 * Copyright (c) 2018-2022 Arm Limited.
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

#include "util/platform_set.hpp"
#include "util/custom_allocator.hpp"
#include "util/unordered_set.hpp"
#include "util/unordered_map.hpp"
#include "util/extension_list.hpp"

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_android.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>

#include <memory>
#include <unordered_set>
#include <cassert>
#include <mutex>

using scoped_mutex = std::lock_guard<std::mutex>;

/** Forward declare stored objects */
namespace wsi
{
class surface;
}

namespace layer
{

/* List of device entrypoints in the layer's instance dispatch table.
 * Note that the Vulkan loader implements some of these entrypoints so the fact that these are non-null doesn't
 * guarantee than we can safely call them. We still mark the entrypoints with REQUIRED() and OPTIONAL(). The layer
 * fails if vkGetInstanceProcAddr returns null for entrypoints that are REQUIRED().
 */
#define INSTANCE_ENTRYPOINTS_LIST(REQUIRED, OPTIONAL)   \
   /* Vulkan 1.0 */                                     \
   REQUIRED(GetInstanceProcAddr)                        \
   REQUIRED(DestroyInstance)                            \
   REQUIRED(GetPhysicalDeviceProperties)                \
   REQUIRED(GetPhysicalDeviceImageFormatProperties)     \
   REQUIRED(EnumerateDeviceExtensionProperties)         \
   REQUIRED(GetPhysicalDeviceMemoryProperties)        \
   /* VK_KHR_surface */                                 \
   OPTIONAL(DestroySurfaceKHR)                          \
   OPTIONAL(GetPhysicalDeviceSurfaceCapabilitiesKHR)    \
   OPTIONAL(GetPhysicalDeviceSurfaceFormatsKHR)         \
   OPTIONAL(GetPhysicalDeviceSurfacePresentModesKHR)    \
   OPTIONAL(GetPhysicalDeviceSurfaceSupportKHR)         \
   /* VK_EXT_headless_surface */                        \
   OPTIONAL(CreateHeadlessSurfaceEXT)                   \
   /* VK_KHR_wayland_surface */                         \
   OPTIONAL(CreateWaylandSurfaceKHR)                    \
   /* VK_KHR_get_surface_capabilities2 */               \
   OPTIONAL(GetPhysicalDeviceSurfaceCapabilities2KHR)   \
   OPTIONAL(GetPhysicalDeviceSurfaceFormats2KHR)        \
   /* VK_KHR_get_physical_device_properties2 or */      \
   /* 1.1 (without KHR suffix) */                       \
   OPTIONAL(GetPhysicalDeviceImageFormatProperties2KHR) \
   OPTIONAL(GetPhysicalDeviceFormatProperties2KHR)      \
   OPTIONAL(GetPhysicalDeviceFeatures2KHR)              \
   /* VK_KHR_device_group + VK_KHR_surface or */        \
   /* 1.1 with VK_KHR_swapchain */                      \
   OPTIONAL(GetPhysicalDevicePresentRectanglesKHR)      \
   /* VK_KHR_external_fence_capabilities or */          \
   /* 1.1 (without KHR suffix) */                       \
   OPTIONAL(GetPhysicalDeviceExternalFencePropertiesKHR)

struct instance_dispatch_table
{
   /**
    * @brief Populate the instance dispatch table with functions that it requires.
    * @note  The function greedy fetches all the functions it needs so even in the
    *        case of failure functions that are not marked as nullptr are safe to call.
    *
    * @param instance The instance for which the dispatch table will be populated.
    * @param get_proc The pointer to vkGetInstanceProcAddr function.
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   VkResult populate(VkInstance instance, PFN_vkGetInstanceProcAddr get_proc);

#define DISPATCH_TABLE_ENTRY(x) PFN_vk##x x{};
   INSTANCE_ENTRYPOINTS_LIST(DISPATCH_TABLE_ENTRY, DISPATCH_TABLE_ENTRY)
#undef DISPATCH_TABLE_ENTRY
};

/* List of device entrypoints in the layer's device dispatch table.
 * The layer fails initializing a device instance when entrypoints marked with REQUIRED() are retrieved as null.
 * The layer will instead tolerate retrieving a null for entrypoints marked as OPTIONAL(). Code in the layer needs to
 * check these entrypoints are non-null before calling them.
 *
 * Note that we cannot rely on checking whether the physical device supports a particular extension as the Vulkan
 * loader currently aggregates all extensions advertised by all implicit layers (in their JSON manifests) and adds
 * them automatically to the output of vkEnumeratePhysicalDeviceProperties.
 */
#define DEVICE_ENTRYPOINTS_LIST(REQUIRED, OPTIONAL) \
   /* Vulkan 1.0 */                                 \
   REQUIRED(GetDeviceQueue)                         \
   REQUIRED(QueueSubmit)                            \
   REQUIRED(QueueWaitIdle)                          \
   REQUIRED(CreateCommandPool)                      \
   REQUIRED(DestroyCommandPool)                     \
   REQUIRED(AllocateCommandBuffers)                 \
   REQUIRED(FreeCommandBuffers)                     \
   REQUIRED(ResetCommandBuffer)                     \
   REQUIRED(BeginCommandBuffer)                     \
   REQUIRED(EndCommandBuffer)                       \
   REQUIRED(CreateImage)                            \
   REQUIRED(DestroyImage)                           \
   REQUIRED(GetImageMemoryRequirements)             \
   REQUIRED(GetImageSubresourceLayout)              \
   REQUIRED(BindImageMemory)                        \
   REQUIRED(AllocateMemory)                         \
   REQUIRED(FreeMemory)                             \
   REQUIRED(CreateFence)                            \
   REQUIRED(DestroyFence)                           \
   REQUIRED(CreateSemaphore)                        \
   REQUIRED(DestroySemaphore)                       \
   REQUIRED(ResetFences)                            \
   REQUIRED(WaitForFences)                          \
   REQUIRED(DestroyDevice)                          \
   /* VK_KHR_swapchain */                           \
   OPTIONAL(CreateSwapchainKHR)                     \
   OPTIONAL(DestroySwapchainKHR)                    \
   OPTIONAL(GetSwapchainImagesKHR)                  \
   OPTIONAL(AcquireNextImageKHR)                    \
   OPTIONAL(QueuePresentKHR)                        \
   /* VK_KHR_device_group + VK_KHR_swapchain or */  \
   /* 1.1 with VK_KHR_swapchain */                  \
   OPTIONAL(AcquireNextImage2KHR)                   \
   /* VK_KHR_device_group + VK_KHR_surface or */    \
   /* 1.1 with VK_KHR_swapchain */                  \
   OPTIONAL(GetDeviceGroupSurfacePresentModesKHR)   \
   OPTIONAL(GetDeviceGroupPresentCapabilitiesKHR)   \
   /* VK_KHR_external_memory_fd */                  \
   OPTIONAL(GetMemoryFdPropertiesKHR)               \
   /* VK_KHR_bind_memory2 or */                     \
   /* 1.1 (without KHR suffix) */                   \
   OPTIONAL(BindImageMemory2KHR)                    \
   /* VK_KHR_external_fence_fd */                   \
   OPTIONAL(GetFenceFdKHR)                          \
   OPTIONAL(ImportFenceFdKHR)                       \
   /* VK_KHR_external_semaphore_fd */               \
   OPTIONAL(ImportSemaphoreFdKHR)                   \
   OPTIONAL(GetMemoryAndroidHardwareBufferANDROID)

struct device_dispatch_table
{
   /**
    * @brief Populate the device dispatch table with functions that it requires.
    * @note  The function greedy fetches all the functions it needs so even in the
    *        case of failure functions that are not marked as nullptr are safe to call.
    *
    * @param device The device for which the dispatch table will be populated.
    * @param get_proc The pointer to vkGetDeviceProcAddr function.
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   VkResult populate(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc);

#define DISPATCH_TABLE_ENTRY(x) PFN_vk##x x{};
   DEVICE_ENTRYPOINTS_LIST(DISPATCH_TABLE_ENTRY, DISPATCH_TABLE_ENTRY)
#undef DISPATCH_TABLE_ENTRY
};

/**
 * @brief Class representing the information that the layer associates to a VkInstance.
 * @details The layer uses this object to store function pointers to use when intercepting a Vulkan call.
 *   Each function intercepted by the layer passes execution to the next layer calling one of these pointers.
 *   Note that the layer does not wrap VkInstance as this would require intercepting every Vulkan entrypoint that has
 *   a VkInstance among its arguments. Instead, the layer maintains a mapping which allows it to retrieve the
 *   #instance_private_data from the VkInstance. To be precise, the mapping uses the VkInstance's dispatch table as a
 *   key, because (1) this is unique for each VkInstance and (2) this allows to map any dispatchable object associated
 *   with the VkInstance (such as VkPhysicalDevice) to the corresponding #instance_private_data (see overloads of
 *   the instance_private_data::get method.)
 */
class instance_private_data
{
public:
   instance_private_data() = delete;
   instance_private_data(const instance_private_data &) = delete;
   instance_private_data &operator=(const instance_private_data &) = delete;

   /**
    * @brief Create and associate a new #instance_private_data to the given #VkInstance.
    *
    * @param instance The instance to associate to the instance_private_data.
    * @param table A populated instance dispatch table.
    * @param set_loader_data The instance loader data.
    * @param enabled_layer_platforms The platforms that are enabled by the layer.
    * @param allocator The allocator that the instance_private_data will use.
    *
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   static VkResult associate(VkInstance instance, instance_dispatch_table &table,
                             util::wsi_platform_set enabled_layer_platforms, const util::allocator &allocator);

   /**
    * @brief Disassociate and destroy the #instance_private_data associated to the given VkInstance.
    *
    * @param instance An instance that was previously associated with instance_private_data
    */
   static void disassociate(VkInstance instance);

   /**
    * @brief Get the mirror object that the layer associates to a given Vulkan instance.
    */
   static instance_private_data &get(VkInstance instance);

   /**
    * @brief Get the layer instance object associated to the VkInstance owning the specified VkPhysicalDevice.
    */
   static instance_private_data &get(VkPhysicalDevice phys_dev);

   /**
    * @brief Associate a VkSurface with a WSI surface object.
    *
    * @param vk_surface  The VkSurface object created by the Vulkan implementation.
    * @param wsi_surface The WSI layer object representing the surface.
    *
    * @return VK_SUCCESS or VK_ERROR_OUT_OF_HOST_MEMORY
    *
    * @note On success this transfers ownership of the WSI surface. The WSI surface is then explicitly destroyed by the
    *       user with @ref remove_surface
    */
   VkResult add_surface(VkSurfaceKHR vk_surface, util::unique_ptr<wsi::surface> &wsi_surface);

   /**
    * @brief Returns any associated WSI surface to the VkSurface.
    *
    * @param vk_surface The VkSurface object queried for association.
    *
    * @return nullptr or a raw pointer to the WSI surface.
    *
    * @note This returns a raw pointer that does not change any ownership. The user is responsible for ensuring that the
    *       pointer is valid as it explicitly controls the lifetime of the object.
    */
   wsi::surface *get_surface(VkSurfaceKHR vk_surface);

   /**
    * @brief Destroys any VkSurface associated WSI surface.
    *
    * @param vk_surface The VkSurface to check for associations.
    * @param alloc      The allocator to use if destroying a @ref wsi::surface object.
    */
   void remove_surface(VkSurfaceKHR vk_surface, const util::allocator &alloc);

   /**
    * @brief Get the set of enabled platforms that are also supported by the layer.
    */
   const util::wsi_platform_set &get_enabled_platforms()
   {
      return enabled_layer_platforms;
   }

   /**
    * @brief Check whether a surface command should be handled by the WSI layer.
    *
    * @param phys_dev Physical device involved in the Vulkan command.
    * @param surface The surface involved in the Vulkan command.
    *
    * @retval @c true if the layer should handle commands for the specified surface, which may mean returning an error
    * if the layer does not support @p surface 's platform.
    *
    * @retval @c false if the layer should call down to the layers and ICDs below to handle the surface commands.
    */
   bool should_layer_handle_surface(VkPhysicalDevice phys_dev, VkSurfaceKHR surface);

   /**
    * @brief Check whether the given surface is supported for presentation via the layer.
    *
    * @param surface A VK_KHR_surface surface.
    *
    * @return Whether the WSI layer supports this surface.
    */
   bool does_layer_support_surface(VkSurfaceKHR surface);

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   /**
    * @brief Check if a physical device supports controlling image compression.
    *
    * @param phys_dev The physical device to query.
    * @return Whether image compression control is supported by the ICD.
    */
   bool has_image_compression_support(VkPhysicalDevice phys_dev);
#endif /* WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN */

   /**
    * @brief Get the instance allocator
    *
    * @return const util::allocator& used for the instance
    */
   const util::allocator &get_allocator() const
   {
      return allocator;
   }

   /**
    * @brief Store the enabled instance extensions.
    *
    * @param extension_names Names of the enabled instance extensions.
    * @param extension_count Size of the enabled instance extensions.
    *
    * @return VK_SUCCESS if successful, otherwise an error.
    */
   VkResult set_instance_enabled_extensions(const char *const *extension_names, size_t extension_count);

   /**
    * @brief Check whether an instance extension is enabled.
    *
    * param extension_name Extension's name.
    *
    * @return true if is enabled, false otherwise.
    */
   bool is_instance_extension_enabled(const char *extension_name) const;

   const instance_dispatch_table disp;

private:
   /* Allow util::allocator to access the private constructor */
   friend util::allocator;

   /**
    * @brief Construct a new instance private data object. This is marked private in order to
    *        ensure that the instance object can only be allocated using the allocator callbacks
    *
    * @param table A populated instance dispatch table.
    * @param set_loader_data The instance loader data.
    * @param enabled_layer_platforms The platforms that are enabled by the layer.
    * @param alloc The allocator that the instance_private_data will use.
    */
   instance_private_data(const instance_dispatch_table &table,
                         util::wsi_platform_set enabled_layer_platforms, const util::allocator &alloc);

   /**
    * @brief Destroy the instance_private_data properly with its allocator
    *
    * @param instance_data A valid pointer to instance_private_data
    */
   static void destroy(instance_private_data *instance_data);

   /**
    * @brief Check whether the given surface is already supported for presentation without the layer.
    */
   bool do_icds_support_surface(VkPhysicalDevice phys_dev, VkSurfaceKHR surface);

   const util::wsi_platform_set enabled_layer_platforms;
   const util::allocator allocator;

   /**
    * @brief Container for all VkSurface objects tracked and supported by the Layer's WSI implementation.
    *
    * Uses plain pointers to store surface data as the lifetime of the object is explicitly controlled by the Vulkan
    * application. The application may also use different but compatible host allocators on creation and destruction.
    */
   util::unordered_map<VkSurfaceKHR, wsi::surface *> surfaces;

   /**
    * @brief Lock for thread safe access to @ref surfaces
    */
   std::mutex surfaces_lock;

   /**
    * @brief List with the names of the enabled instance extensions.
    */
   util::extension_list enabled_extensions;
};

/**
 * @brief Class representing the information that the layer associates to a VkDevice.
 * @note This serves a similar purpose of #instance_private_data, but for VkDevice. Similarly to
 *   #instance_private_data, the layer maintains a mapping from VkDevice to the associated #device_private_data.
 */
class device_private_data
{
public:
   device_private_data() = delete;
   device_private_data(const device_private_data &) = delete;
   device_private_data &operator=(const device_private_data &) = delete;

   /**
    * @brief Create and associate a new #device_private_data to the given #VkDevice.
    *
    * @param dev The device to associate to the device_private_data.
    * @param inst_data The instance that was used to create VkDevice.
    * @param phys_dev The physical device that was used to create the VkDevice.
    * @param table A populated device dispatch table.
    * @param set_loader_data The device loader data.
    * @param allocator The allocator that the device_private_data will use.
    *
    * @return VkResult VK_SUCCESS if successful, otherwise an error
    */
   static VkResult associate(VkDevice dev, instance_private_data &inst_data, VkPhysicalDevice phys_dev,
                             const device_dispatch_table &table, PFN_vkSetDeviceLoaderData set_loader_data,
                             const util::allocator &allocator, std::vector<VkQueue>& queues);

   static void disassociate(VkDevice dev);

   /**
    * @brief Get the mirror object that the layer associates to a given Vulkan device.
    */
   static device_private_data &get(VkDevice device);

   /**
    * @brief Get the layer device object associated to the VkDevice owning the specified VkQueue.
    */
   static device_private_data &get(VkQueue queue);

   /**
    * @brief Add a swapchain to the swapchains member variable.
    */
   VkResult add_layer_swapchain(VkSwapchainKHR swapchain);

   /**
    * @brief Remove a swapchain from the swapchains member variable.
    */
   void remove_layer_swapchain(VkSwapchainKHR swapchain);

   /**
    * @brief Return whether all the provided swapchains are owned by us (the WSI Layer).
    */
   bool layer_owns_all_swapchains(const VkSwapchainKHR *swapchain, uint32_t swapchain_count) const;

   /**
    * @brief Check whether the given swapchain is owned by us (the WSI Layer).
    */
   bool layer_owns_swapchain(VkSwapchainKHR swapchain) const
   {
      return layer_owns_all_swapchains(&swapchain, 1);
   }

   /**
    * @brief Check whether the layer can create a swapchain for the given surface.
    */
   bool should_layer_create_swapchain(VkSurfaceKHR vk_surface);

   /**
    * @brief Check whether the ICDs or layers below support VK_KHR_swapchain.
    */
   bool can_icds_create_swapchain(VkSurfaceKHR vk_surface);

   /**
    * @brief Get the device allocator
    *
    * @return const util::allocator& used for the device
    */
   const util::allocator &get_allocator() const
   {
      return allocator;
   }

   /**
    * @brief Store the enabled device extensions.
    *
    * @param extension_names Names of the enabled device extensions.
    * @param extension_count Size of the enabled device extensions.
    *
    * @return VK_SUCCESS if successful, otherwise an error.
    */
   VkResult set_device_enabled_extensions(const char *const *extension_names, size_t extension_count);

   /**
    * @brief Check whether a device extension is enabled.
    *
    * param extension_name Extension's name.
    *
    * @return true if is enabled, false otherwise.
    */
   bool is_device_extension_enabled(const char *extension_name) const;

   const device_dispatch_table disp;
   instance_private_data &instance_data;
   const PFN_vkSetDeviceLoaderData SetDeviceLoaderData;
   const VkPhysicalDevice physical_device;
   const VkDevice device;

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   /**
    * @brief Set whether the device supports controlling the swapchain image compression.
    *
    * @param enable Value to set compression_control_enabled member variable.
    */
   void set_swapchain_compression_control_enabled(bool enable);

   /**
    * @brief Check whether the device supports controlling the swapchain image compression.
    *
    * @return true if enabled, false otherwise.
    */
   bool is_swapchain_compression_control_enabled() const;
#endif /* WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN */

private:
   /* Allow util::allocator to access the private constructor */
   friend util::allocator;

   /**
    * @brief Construct a new device private data object. This is marked private in order to
    *        ensure that the instance object can only be allocated using the allocator callbacks
    *
    * @param inst_data The instance that was used to create VkDevice.
    * @param phys_dev The physical device that was used to create the VkDevice.
    * @param dev The device to associate to the device_private_data.
    * @param table A populated device dispatch table.
    * @param set_loader_data The device loader data.
    * @param alloc The allocator that the device_private_data will use.
    */
   device_private_data(instance_private_data &inst_data, VkPhysicalDevice phys_dev, VkDevice dev,
                       const device_dispatch_table &table, PFN_vkSetDeviceLoaderData set_loader_data,
                       const util::allocator &alloc);

   /**
    * @brief Destroy the device_private_data properly with its allocator
    *
    * @param device_data A valid pointer to device_private_data
    */
   static void destroy(device_private_data *device_data);

   const util::allocator allocator;
   util::unordered_set<VkSwapchainKHR> swapchains;
   mutable std::mutex swapchains_lock;

   /**
    * @brief List with the names of the enabled device extensions.
    */
   util::extension_list enabled_extensions;

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   /**
    * @brief Stores whether the device supports controlling the swapchain image compression.
    *
    */
   bool compression_control_enabled;
#endif /* WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN */
};

} /* namespace layer */
