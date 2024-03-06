/*
 * Copyright (c) 2018-2022, 2024 Arm Limited.
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
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan_wayland.h>

#include <memory>
#include <unordered_set>
#include <cassert>
#include <mutex>
#include <limits>
#include <cstring>

using scoped_mutex = std::lock_guard<std::mutex>;

/** Forward declare stored objects */
namespace wsi
{
class surface;
}

namespace layer
{

/**
 * @brief Definition of an entrypoint.
 */
struct entrypoint
{
   const char *name;
   const char *ext_name;
   PFN_vkVoidFunction fn;
   uint32_t api_version;
   bool user_visible;
   bool required;
};

/**
 * @brief Dispatch table base.
 *
 * This struct defines generic get and call function templates for a dispatch table.
 */
class dispatch_table
{
public:
   using entrypoint_list = util::unordered_map<std::string, entrypoint>;

   /**
    * @brief Construct a new dispatch table object
    *
    * @param allocator Pre-allocated entrypoint storage container
    */
   dispatch_table(util::unique_ptr<entrypoint_list> entrypoints)
      : m_entrypoints(std::move(entrypoints))
   {
      /* This pointer is expected to be valid */
      assert(m_entrypoints != nullptr);
   }

   /**
    * @brief Get the function object from the entrypoints.
    *
    * @tparam FunctionType The signature of the requested function.
    * @param fn_name The name of the function.
    * @return the requested function pointer, or std::nullopt.
    */
   template <typename FunctionType>
   std::optional<FunctionType> get_fn(const char *fn_name) const
   {
      auto fn = m_entrypoints->find(fn_name);
      if (fn != m_entrypoints->end())
      {
         return reinterpret_cast<FunctionType>(fn->second.fn);
      }

      return std::nullopt;
   }

   /**
    * @brief Set the user enabled extensions.
    *
    * @param extension_names Names of the extensions enabled by user.
    * @param extension_count Number of extensions enabled by the user.
    */
   void set_user_enabled_extensions(const char *const *extension_names, size_t extension_count);

protected:
   /**
    * @brief Call function from the dispatch table entrypoints.
    *
    * @tparam FunctionType The signature of the function to call.
    * @tparam Args Argument types of the function to call.
    *
    * @param fn_name Name of the function to call.
    * @param args Arguments to the function to call.
    * @return function return value or std::nullopt if function is not present in entrypoints
    */
   template <
      typename FunctionType, class... Args, typename ReturnType = std::invoke_result_t<FunctionType, Args...>,
      std::enable_if_t<!std::is_void<ReturnType>::value && !std::is_same<ReturnType, VkResult>::value, bool> = true>
   std::optional<ReturnType> call_fn(const char *fn_name, Args &&...args) const
   {
      auto fn = get_fn<FunctionType>(fn_name);
      if (fn.has_value())
      {
         return (*fn)(std::forward<Args>(args)...);
      }

      WSI_LOG_WARNING("Call to %s failed, dispatch table does not contain the function.", fn_name);

      return std::nullopt;
   }

   /**
    * @brief Call function from the dispatch table entrypoints.
    * @note This overload matches for functions with void return type.
    *
    * @tparam FunctionType The signature of the function to call.
    * @tparam Args Argument types of the function to call.
    *
    * @param fn_name Name of the function to call.
    * @param args Arguments to the function to call.
    */
   template <typename FunctionType, class... Args, typename ReturnType = std::invoke_result_t<FunctionType, Args...>,
             std::enable_if_t<std::is_void<ReturnType>::value, bool> = true>
   void call_fn(const char *fn_name, Args &&...args) const
   {
      auto fn = get_fn<FunctionType>(fn_name);
      if (fn.has_value())
      {
         return (*fn)(std::forward<Args>(args)...);
      }

      WSI_LOG_WARNING("Call to %s failed, dispatch table does not contain the function.", fn_name);
   }

   /**
    * @brief Call function from the dispatch table entrypoints.
    * @note This overload matches for functions with VkResult return type.
    *
    * @tparam FunctionType The signature of the function to call.
    * @tparam Args Argument types of the function to call.
    *
    * @param fn_name Name of the function to call.
    * @param args Arguments to the function to call.
    * @return function return value or VK_ERROR_EXTENSION_NOT_PRESENT if function is not present in entrypoints
    */
   template <typename FunctionType, class... Args, typename ReturnType = std::invoke_result_t<FunctionType, Args...>,
             std::enable_if_t<std::is_same<ReturnType, VkResult>::value, bool> = true>
   VkResult call_fn(const char *fn_name, Args &&...args) const
   {
      auto fn = get_fn<FunctionType>(fn_name);
      if (fn.has_value())
      {
         return (*fn)(std::forward<Args>(args)...);
      }

      WSI_LOG_WARNING("Call to %s failed, dispatch table does not contain the function.", fn_name);

      return VK_ERROR_EXTENSION_NOT_PRESENT;
   }

   /** @brief Vector that holds the entrypoints of the dispatch table */
   util::unique_ptr<entrypoint_list> m_entrypoints;
};

/* Represents the maximum possible Vulkan API version. */
static constexpr uint32_t API_VERSION_MAX = UINT32_MAX;

/* List of instance entrypoints in the layer's instance dispatch table.
 * Note that the Vulkan loader implements some of these entrypoints so the fact that these are non-null doesn't
 * guarantee than we can safely call them. We still mark the entrypoints required == true / false. The layer
 * fails if vkGetInstanceProcAddr returns null for entrypoints that are required.
 *
 * Format of an entry is: EP(entrypoint_name, extension_name, api_version, required)
 * entrypoint_name: Name of the entrypoint.
 * extension_name: Name of the extension that provides the entrypoint.
 * api_version: Vulkan API version where the entrypoint is part of the core specification, or API_VERSION_MAX.
 * required: Boolean to indicate whether the entrypoint is required by the WSI layer or optional.
 */
#define INSTANCE_ENTRYPOINTS_LIST(EP)                                                                                \
   /* Vulkan 1.0 */                                                                                                  \
   EP(GetInstanceProcAddr, "", VK_API_VERSION_1_0, true)                                                             \
   EP(DestroyInstance, "", VK_API_VERSION_1_0, true)                                                                 \
   EP(GetPhysicalDeviceProperties, "", VK_API_VERSION_1_0, true)                                                     \
   EP(GetPhysicalDeviceImageFormatProperties, "", VK_API_VERSION_1_0, true)                                          \
   EP(EnumerateDeviceExtensionProperties, "", VK_API_VERSION_1_0, true)                                              \
   /* VK_KHR_surface */                                                                                              \
   EP(DestroySurfaceKHR, VK_KHR_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                                      \
   EP(GetPhysicalDeviceSurfaceCapabilitiesKHR, VK_KHR_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                \
   EP(GetPhysicalDeviceSurfaceFormatsKHR, VK_KHR_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                     \
   EP(GetPhysicalDeviceSurfacePresentModesKHR, VK_KHR_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                \
   EP(GetPhysicalDeviceSurfaceSupportKHR, VK_KHR_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                     \
   /* VK_EXT_headless_surface */                                                                                     \
   EP(CreateHeadlessSurfaceEXT, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                      \
   /* VK_KHR_wayland_surface */                                                                                      \
   EP(CreateWaylandSurfaceKHR, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, API_VERSION_MAX, false)                        \
   /* VK_KHR_display */                                                                                              \
   EP(CreateDisplayPlaneSurfaceKHR, VK_KHR_DISPLAY_EXTENSION_NAME, API_VERSION_MAX, false)                           \
   /* VK_KHR_get_surface_capabilities2 */                                                                            \
   EP(GetPhysicalDeviceSurfaceCapabilities2KHR, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, API_VERSION_MAX,   \
      false)                                                                                                         \
   EP(GetPhysicalDeviceSurfaceFormats2KHR, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, API_VERSION_MAX, false) \
   /* VK_KHR_get_physical_device_properties2 or */                                                                   \
   /* 1.1 (without KHR suffix) */                                                                                    \
   EP(GetPhysicalDeviceImageFormatProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,            \
      VK_API_VERSION_1_1, false)                                                                                     \
   EP(GetPhysicalDeviceFormatProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,                 \
      VK_API_VERSION_1_1, false)                                                                                     \
   EP(GetPhysicalDeviceFeatures2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, VK_API_VERSION_1_1,     \
      false)                                                                                                         \
   EP(GetPhysicalDeviceProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, VK_API_VERSION_1_1,   \
      false)                                                                                                         \
   EP(GetPhysicalDeviceQueueFamilyProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,            \
      VK_API_VERSION_1_1, false)                                                                                     \
   EP(GetPhysicalDeviceMemoryProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,                 \
      VK_API_VERSION_1_1, false)                                                                                     \
   EP(GetPhysicalDeviceSparseImageFormatProperties2KHR, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,      \
      VK_API_VERSION_1_1, false)                                                                                     \
   /* VK_KHR_device_group + VK_KHR_surface or */                                                                     \
   /* 1.1 with VK_KHR_swapchain */                                                                                   \
   EP(GetPhysicalDevicePresentRectanglesKHR, VK_KHR_DEVICE_GROUP_EXTENSION_NAME, VK_API_VERSION_1_1, false)          \
   /* VK_KHR_external_fence_capabilities or */                                                                       \
   /* 1.1 (without KHR suffix) */                                                                                    \
   EP(GetPhysicalDeviceExternalFencePropertiesKHR, VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,                \
      VK_API_VERSION_1_1, false)                                                                                     \
   EP(GetPhysicalDeviceExternalBufferPropertiesKHR, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,              \
      VK_API_VERSION_1_1, false)

/**
 * @brief Struct representing the instance dispatch table.
 */
class instance_dispatch_table : public dispatch_table
{
public:
   static std::optional<instance_dispatch_table> create(const util::allocator &allocator)
   {
      auto entrypoints = allocator.make_unique<dispatch_table::entrypoint_list>(allocator);
      if (entrypoints == nullptr)
      {
         return std::nullopt;
      }

      return instance_dispatch_table{ std::move(entrypoints) };
   }

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

   /**
    * @brief Get the user enabled instance extension entrypoint by name
    *
    * @param instance The Vulkan instance that the extension was enabled on.
    * @param api_version The API version of the Vulkan instance.
    * @param fn_name The name of the function.
    * @return pointer to the function if it is enabled by the user, otherwise nullptr.
    */
   PFN_vkVoidFunction get_user_enabled_entrypoint(VkInstance instance, uint32_t api_version, const char *fn_name) const;

   /* Generate alias functions for internal use of the dispatch table entrypoints.
    * These will be named as the entrypoint, but without the "vk" prefix.
    * Assuming disp is an instance of instance_dispatch_table, the following syntax is supported:
    *    disp.GetInstanceProcAddr(instance, fn_name);
    * The result type will be matching the function signature, so there is no need for casting.
    */
#define DISPATCH_TABLE_SHORTCUT(name, unused1, unused2, unused3)             \
   template <class... Args>                                                  \
   auto name(Args &&...args) const                                           \
   {                                                                         \
      return call_fn<PFN_vk##name>("vk" #name, std::forward<Args>(args)...); \
   };

   INSTANCE_ENTRYPOINTS_LIST(DISPATCH_TABLE_SHORTCUT)
#undef DISPATCH_TABLE_SHORTCUT

private:
   /**
    * @brief Construct instance dispatch table object
    *
    * @param table Pre-allocated dispatch table
    */
   instance_dispatch_table(util::unique_ptr<dispatch_table::entrypoint_list> table)
      : dispatch_table{ std::move(table) }
   {
   }
};

/* List of device entrypoints in the layer's device dispatch table.
 * The layer fails initializing a device instance when entrypoints marked with REQUIRED() are retrieved as null.
 * The layer will instead tolerate retrieving a null for entrypoints marked as OPTIONAL(). Code in the layer needs to
 * check these entrypoints are non-null before calling them.
 *
 * Note that we cannot rely on checking whether the physical device supports a particular extension as the Vulkan
 * loader currently aggregates all extensions advertised by all implicit layers (in their JSON manifests) and adds
 * them automatically to the output of vkEnumeratePhysicalDeviceProperties.
 *
 * Format of an entry is: EP(entrypoint_name, extension_name, api_version, required)
 * entrypoint_name: Name of the entrypoint.
 * extension_name: Name of the extension that provides the entrypoint.
 * api_version: Vulkan API version where the entrypoint is part of the core specification, or API_VERSION_MAX.
 * required: Boolean to indicate whether the entrypoint is required by the WSI layer or optional.
 */
#define DEVICE_ENTRYPOINTS_LIST(EP)                                                                                    \
   /* Vulkan 1.0 */                                                                                                    \
   EP(GetDeviceProcAddr, "", VK_API_VERSION_1_0, true)                                                                 \
   EP(GetDeviceQueue, "", VK_API_VERSION_1_0, true)                                                                    \
   EP(QueueSubmit, "", VK_API_VERSION_1_0, true)                                                                       \
   EP(QueueWaitIdle, "", VK_API_VERSION_1_0, true)                                                                     \
   EP(CreateCommandPool, "", VK_API_VERSION_1_0, true)                                                                 \
   EP(DestroyCommandPool, "", VK_API_VERSION_1_0, true)                                                                \
   EP(AllocateCommandBuffers, "", VK_API_VERSION_1_0, true)                                                            \
   EP(FreeCommandBuffers, "", VK_API_VERSION_1_0, true)                                                                \
   EP(ResetCommandBuffer, "", VK_API_VERSION_1_0, true)                                                                \
   EP(BeginCommandBuffer, "", VK_API_VERSION_1_0, true)                                                                \
   EP(EndCommandBuffer, "", VK_API_VERSION_1_0, true)                                                                  \
   EP(CreateImage, "", VK_API_VERSION_1_0, true)                                                                       \
   EP(DestroyImage, "", VK_API_VERSION_1_0, true)                                                                      \
   EP(GetImageMemoryRequirements, "", VK_API_VERSION_1_0, true)                                                        \
   EP(BindImageMemory, "", VK_API_VERSION_1_0, true)                                                                   \
   EP(AllocateMemory, "", VK_API_VERSION_1_0, true)                                                                    \
   EP(FreeMemory, "", VK_API_VERSION_1_0, true)                                                                        \
   EP(CreateFence, "", VK_API_VERSION_1_0, true)                                                                       \
   EP(DestroyFence, "", VK_API_VERSION_1_0, true)                                                                      \
   EP(CreateSemaphore, "", VK_API_VERSION_1_0, true)                                                                   \
   EP(DestroySemaphore, "", VK_API_VERSION_1_0, true)                                                                  \
   EP(ResetFences, "", VK_API_VERSION_1_0, true)                                                                       \
   EP(WaitForFences, "", VK_API_VERSION_1_0, true)                                                                     \
   EP(DestroyDevice, "", VK_API_VERSION_1_0, true)                                                                     \
   /* VK_KHR_swapchain */                                                                                              \
   EP(CreateSwapchainKHR, VK_KHR_SWAPCHAIN_EXTENSION_NAME, API_VERSION_MAX, false)                                     \
   EP(DestroySwapchainKHR, VK_KHR_SWAPCHAIN_EXTENSION_NAME, API_VERSION_MAX, false)                                    \
   EP(GetSwapchainImagesKHR, VK_KHR_SWAPCHAIN_EXTENSION_NAME, API_VERSION_MAX, false)                                  \
   EP(AcquireNextImageKHR, VK_KHR_SWAPCHAIN_EXTENSION_NAME, API_VERSION_MAX, false)                                    \
   EP(QueuePresentKHR, VK_KHR_SWAPCHAIN_EXTENSION_NAME, API_VERSION_MAX, false)                                        \
   /* VK_KHR_shared_presentable_image */                                                                               \
   EP(GetSwapchainStatusKHR, VK_KHR_SHARED_PRESENTABLE_IMAGE_EXTENSION_NAME, API_VERSION_MAX, false)                   \
   /* VK_KHR_device_group + VK_KHR_swapchain or */                                                                     \
   /* 1.1 with VK_KHR_swapchain */                                                                                     \
   EP(AcquireNextImage2KHR, VK_KHR_DEVICE_GROUP_EXTENSION_NAME, VK_API_VERSION_1_1, false)                             \
   /* VK_KHR_device_group + VK_KHR_surface or */                                                                       \
   /* 1.1 with VK_KHR_swapchain */                                                                                     \
   EP(GetDeviceGroupSurfacePresentModesKHR, VK_KHR_DEVICE_GROUP_EXTENSION_NAME, VK_API_VERSION_1_1, false)             \
   EP(GetDeviceGroupPresentCapabilitiesKHR, VK_KHR_DEVICE_GROUP_EXTENSION_NAME, VK_API_VERSION_1_1, false)             \
   /* VK_KHR_external_memory_fd */                                                                                     \
   EP(GetMemoryFdKHR, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, API_VERSION_MAX, false)                                \
   EP(GetMemoryFdPropertiesKHR, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, API_VERSION_MAX, false)                      \
   /* VK_KHR_bind_memory2 or */                                                                                        \
   /* 1.1 (without KHR suffix) */                                                                                      \
   EP(BindImageMemory2KHR, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, VK_API_VERSION_1_1, false)                             \
   EP(BindBufferMemory2KHR, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, VK_API_VERSION_1_1, false)                            \
   /* VK_KHR_external_fence_fd */                                                                                      \
   EP(GetFenceFdKHR, VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME, API_VERSION_MAX, false)                                  \
   EP(ImportFenceFdKHR, VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME, API_VERSION_MAX, false)                               \
   /* VK_KHR_external_semaphore_fd */                                                                                  \
   EP(ImportSemaphoreFdKHR, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, API_VERSION_MAX, false)                       \
   EP(GetSemaphoreFdKHR, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, API_VERSION_MAX, false)                          \
   /* VK_KHR_image_drm_format_modifier */                                                                              \
   EP(GetImageDrmFormatModifierPropertiesEXT, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, API_VERSION_MAX, false) \
   /* VK_KHR_sampler_ycbcr_conversion */                                                                               \
   EP(CreateSamplerYcbcrConversionKHR, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_API_VERSION_1_1, false)      \
   EP(DestroySamplerYcbcrConversionKHR, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_API_VERSION_1_1, false)     \
   /* VK_KHR_maintenance1 */                                                                                           \
   EP(TrimCommandPoolKHR, VK_KHR_MAINTENANCE1_EXTENSION_NAME, VK_API_VERSION_1_1, false)                               \
   /* VK_KHR_get_memory_requirements2 */                                                                               \
   EP(GetImageMemoryRequirements2KHR, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_API_VERSION_1_1, false)      \
   EP(GetBufferMemoryRequirements2KHR, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_API_VERSION_1_1, false)     \
   EP(GetImageSparseMemoryRequirements2KHR, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_API_VERSION_1_1, false)

/**
 * @brief Struct representing the device dispatch table.
 */
class device_dispatch_table : public dispatch_table
{
public:
   static std::optional<device_dispatch_table> create(const util::allocator &allocator)
   {
      auto entrypoints = allocator.make_unique<dispatch_table::entrypoint_list>(allocator);
      if (entrypoints == nullptr)
      {
         return std::nullopt;
      }

      return device_dispatch_table{ std::move(entrypoints) };
   }

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

   /**
    * @brief Get the user enabled device extension entrypoint by name
    *
    * @param device The Vulkan device that the extension was enabled on.
    * @param api_version The API version of the Vulkan instance.
    * @param fn_name The name of the function.
    * @return pointer to the function if it is enabled by the user, otherwise nullptr.
    */
   PFN_vkVoidFunction get_user_enabled_entrypoint(VkDevice device, uint32_t api_version, const char *fn_name) const;

   /* Generate alias functions for internal use of the dispatch table entrypoints.
    * These will be named as the entrypoint, but without the "vk" prefix.
    * Assuming disp is an instance of device_dispatch_table, the following syntax is supported:
    *    disp.GetDeviceProcAddr(instance, fn_name);
    * The result type will be matching the function signature, so there is no need for casting.
    */
#define DISPATCH_TABLE_SHORTCUT(name, unused1, unused2, unused3)             \
   template <class... Args>                                                  \
   auto name(Args &&...args) const                                           \
   {                                                                         \
      return call_fn<PFN_vk##name>("vk" #name, std::forward<Args>(args)...); \
   };

   DEVICE_ENTRYPOINTS_LIST(DISPATCH_TABLE_SHORTCUT)
#undef DISPATCH_TABLE_SHORTCUT

private:
   /**
    * @brief Construct instance dispatch table object
    *
    * @param table Pre-allocated dispatch table
    */
   device_dispatch_table(util::unique_ptr<dispatch_table::entrypoint_list> table)
      : dispatch_table{ std::move(table) }
   {
   }
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
   static VkResult associate(VkInstance instance, instance_dispatch_table table,
                             PFN_vkSetInstanceLoaderData set_loader_data,
                             util::wsi_platform_set enabled_layer_platforms, const uint32_t api_version,
                             const util::allocator &allocator);

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
   const uint32_t api_version;

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
   instance_private_data(instance_dispatch_table table, PFN_vkSetInstanceLoaderData set_loader_data,
                         util::wsi_platform_set enabled_layer_platforms, const uint32_t api_version,
                         const util::allocator &alloc);

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

   const PFN_vkSetInstanceLoaderData SetInstanceLoaderData;
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
                             device_dispatch_table table, PFN_vkSetDeviceLoaderData set_loader_data,
                             const util::allocator &allocator);

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
                       device_dispatch_table table, PFN_vkSetDeviceLoaderData set_loader_data,
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
