/*
 * Copyright (c) 2018-2021 Arm Limited.
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

#include "private_data.hpp"
#include "wsi/wsi_factory.hpp"
#include "wsi/surface.hpp"
#include "util/unordered_map.hpp"
#include "util/log.hpp"

namespace layer
{

static std::mutex g_data_lock;

/* The dictionaries below use plain pointers to store the instance/device private data objects.
 * This means that these objects are leaked if the application terminates without calling vkDestroyInstance
 * or vkDestroyDevice. This is fine as it is the application's responsibility to call these.
 */
static util::unordered_map<void *, instance_private_data *> g_instance_data{ util::allocator::get_generic() };
static util::unordered_map<void *, device_private_data *> g_device_data{ util::allocator::get_generic() };

static const std::array<const char *, 3> supported_instance_extensions = {
   VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
};

static const std::array<const char *, 1> supported_device_extensions = {
   VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

template <typename object_type, typename get_proc_type>
static PFN_vkVoidFunction get_proc_helper(object_type obj, get_proc_type get_proc,
                                          const char* proc_name, bool required, bool &ok)
{
   PFN_vkVoidFunction ret = get_proc(obj, proc_name);
   if (nullptr == ret && required)
   {
      ok = false;
   }
   return ret;
}

VkResult instance_dispatch_table::populate(VkInstance instance, PFN_vkGetInstanceProcAddr get_proc)
{
   bool ok = true;
#define REQUIRED(x) x = reinterpret_cast<PFN_vk##x>(get_proc_helper(instance, get_proc, "vk" #x, true, ok));
#define OPTIONAL(x) x = reinterpret_cast<PFN_vk##x>(get_proc_helper(instance, get_proc, "vk" #x, false, ok));
   INSTANCE_ENTRYPOINTS_LIST(REQUIRED, OPTIONAL);
#undef REQUIRED
#undef OPTIONAL
   return ok ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

VkResult device_dispatch_table::populate(VkDevice device, PFN_vkGetDeviceProcAddr get_proc)
{
   bool ok = true;
#define REQUIRED(x) x = reinterpret_cast<PFN_vk##x>(get_proc_helper(device, get_proc, "vk" #x, true, ok));
#define OPTIONAL(x) x = reinterpret_cast<PFN_vk##x>(get_proc_helper(device, get_proc, "vk" #x, false, ok));
   DEVICE_ENTRYPOINTS_LIST(REQUIRED, OPTIONAL);
#undef REQUIRED
#undef OPTIONAL
   return ok ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

instance_private_data::instance_private_data(const instance_dispatch_table &table,
                                             PFN_vkSetInstanceLoaderData set_loader_data,
                                             util::wsi_platform_set enabled_layer_platforms,
                                             const util::allocator &alloc)
   : disp{ table }
   , SetInstanceLoaderData{ set_loader_data }
   , enabled_layer_platforms{ enabled_layer_platforms }
   , allocator{ alloc }
   , surfaces{ alloc }
   , enabled_extensions{ allocator }
{
}

/**
 * @brief Obtain the loader's dispatch table for the given dispatchable object.
 * @note Dispatchable objects are structures that have a VkLayerDispatchTable as their first member.
 */
template <typename dispatchable_type>
static inline void *get_key(dispatchable_type dispatchable_object)
{
   return *reinterpret_cast<void **>(dispatchable_object);
}

VkResult instance_private_data::associate(VkInstance instance, instance_dispatch_table &table,
                                          PFN_vkSetInstanceLoaderData set_loader_data,
                                          util::wsi_platform_set enabled_layer_platforms, const util::allocator &allocator)
{
   auto instance_data =
      allocator.make_unique<instance_private_data>(table, set_loader_data, enabled_layer_platforms, allocator);

   if (instance_data == nullptr)
   {
      WSI_LOG_ERROR("Instance private data for instance(%p) could not be allocated. Out of memory.",
                    reinterpret_cast<void *>(instance));
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   const auto key = get_key(instance);
   scoped_mutex lock(g_data_lock);

   auto it = g_instance_data.find(key);
   if (it != g_instance_data.end())
   {
      WSI_LOG_WARNING("Hash collision when adding new instance (%p)", reinterpret_cast<void *>(instance));

      destroy(it->second);
      g_instance_data.erase(it);
   }

   auto result = g_instance_data.try_insert(std::make_pair(key, instance_data.get()));
   if (result.has_value())
   {
      instance_data.release(); // NOLINT(bugprone-unused-return-value)
      return VK_SUCCESS;
   }
   else
   {
      WSI_LOG_WARNING("Failed to insert instance_private_data for instance (%p) as host is out of memory",
                      reinterpret_cast<void *>(instance));

      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
}

void instance_private_data::disassociate(VkInstance instance)
{
   assert(instance != VK_NULL_HANDLE);
   instance_private_data *instance_data = nullptr;
   {
      scoped_mutex lock(g_data_lock);
      auto it = g_instance_data.find(get_key(instance));
      if (it == g_instance_data.end())
      {
         WSI_LOG_WARNING("Failed to find private data for instance (%p)", reinterpret_cast<void *>(instance));
         return;
      }

      instance_data = it->second;
      g_instance_data.erase(it);
   }

   destroy(instance_data);
}

template <typename dispatchable_type>
static instance_private_data &get_instance_private_data(dispatchable_type dispatchable_object)
{
   scoped_mutex lock(g_data_lock);
   return *g_instance_data.at(get_key(dispatchable_object));
}

instance_private_data &instance_private_data::get(VkInstance instance)
{
   return get_instance_private_data(instance);
}

instance_private_data &instance_private_data::get(VkPhysicalDevice phys_dev)
{
   return get_instance_private_data(phys_dev);
}

VkResult instance_private_data::add_surface(VkSurfaceKHR vk_surface, util::unique_ptr<wsi::surface> &wsi_surface)
{
   scoped_mutex lock(surfaces_lock);

   auto it = surfaces.find(vk_surface);
   if (it != surfaces.end())
   {
      WSI_LOG_WARNING("Hash collision when adding new surface (%p). Old surface is replaced.",
                      reinterpret_cast<void *>(vk_surface));
      surfaces.erase(it);
   }

   auto result = surfaces.try_insert(std::make_pair(vk_surface, nullptr));
   if (result.has_value())
   {
      assert(result->second);
      result->first->second = wsi_surface.release();
      return VK_SUCCESS;
   }

   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

wsi::surface *instance_private_data::get_surface(VkSurfaceKHR vk_surface)
{
   scoped_mutex lock(surfaces_lock);
   auto it = surfaces.find(vk_surface);
   if (it != surfaces.end())
   {
      return it->second;
   }

   return nullptr;
}

void instance_private_data::remove_surface(VkSurfaceKHR vk_surface, const util::allocator &alloc)
{
   scoped_mutex lock(surfaces_lock);
   auto it = surfaces.find(vk_surface);
   if (it != surfaces.end())
   {
      alloc.destroy<wsi::surface>(1, it->second);
      surfaces.erase(it);
   }
   /* Failing to find a surface is not an error. It could have been created by a WSI extension, which is not handled
    * by this layer.
    */
}

bool instance_private_data::does_layer_support_surface(VkSurfaceKHR surface)
{
   scoped_mutex lock(surfaces_lock);
   auto it = surfaces.find(surface);
   return it != surfaces.end();
}

void instance_private_data::destroy(instance_private_data *instance_data)
{
   assert(instance_data);

   auto alloc = instance_data->get_allocator();
   alloc.destroy<instance_private_data>(1, instance_data);
}

bool instance_private_data::do_icds_support_surface(VkPhysicalDevice, VkSurfaceKHR)
{
   /* For now assume ICDs do not support VK_KHR_surface. This means that the layer will handle all the surfaces it can
    * handle (even if the ICDs can handle the surface) and only call down for surfaces it cannot handle. In the future
    * we may allow system integrators to configure which ICDs have precedence handling which platforms.
    */
   return false;
}

bool instance_private_data::should_layer_handle_surface(VkPhysicalDevice phys_dev, VkSurfaceKHR surface)
{
   /* If the layer cannot handle the surface, then necessarily the ICDs or layers below us must be able to do it:
    * the fact that the surface exists means that the Vulkan loader created it. In turn, this means that someone
    * among the ICDs and layers advertised support for it. If it's not us, then it must be one of the layers/ICDs
    * below us. It is therefore safe to always return false (and therefore call-down) when layer_can_handle_surface
    * is false.
    */
   bool icd_can_handle_surface = do_icds_support_surface(phys_dev, surface);
   bool layer_can_handle_surface = does_layer_support_surface(surface);
   bool ret = layer_can_handle_surface && !icd_can_handle_surface;
   return ret;
}

VkResult instance_private_data::set_instance_enabled_extensions(const char *const *extension_names,
                                                                size_t extension_count)
{
   return enabled_extensions.add(extension_names, extension_count, supported_instance_extensions.data(),
                                 supported_instance_extensions.size());
}

bool instance_private_data::is_instance_extension_enabled(const char *extension_name) const
{
   return enabled_extensions.contains(extension_name);
}

device_private_data::device_private_data(instance_private_data &inst_data, VkPhysicalDevice phys_dev, VkDevice dev,
                                         const device_dispatch_table &table, PFN_vkSetDeviceLoaderData set_loader_data,
                                         const util::allocator &alloc)
   : disp{ table }
   , instance_data{ inst_data }
   , SetDeviceLoaderData{ set_loader_data }
   , physical_device{ phys_dev }
   , device{ dev }
   , allocator{ alloc }
   , swapchains{ allocator }
   , enabled_extensions{ allocator }
{
}

VkResult device_private_data::associate(VkDevice dev, instance_private_data &inst_data, VkPhysicalDevice phys_dev,
                                        const device_dispatch_table &table, PFN_vkSetDeviceLoaderData set_loader_data,
                                        const util::allocator &allocator)
{
   auto device_data =
      allocator.make_unique<device_private_data>(inst_data, phys_dev, dev, table, set_loader_data, allocator);

   if (device_data == nullptr)
   {
      WSI_LOG_ERROR("Device private data for device(%p) could not be allocated. Out of memory.",
                    reinterpret_cast<void *>(dev));
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   const auto key = get_key(dev);
   scoped_mutex lock(g_data_lock);

   auto it = g_device_data.find(key);
   if (it != g_device_data.end())
   {
      WSI_LOG_WARNING("Hash collision when adding new device (%p)", reinterpret_cast<void *>(dev));
      destroy(it->second);
      g_device_data.erase(it);
   }

   auto result = g_device_data.try_insert(std::make_pair(key, device_data.get()));
   if (result.has_value())
   {
      device_data.release(); // NOLINT(bugprone-unused-return-value)
      return VK_SUCCESS;
   }
   else
   {
      WSI_LOG_WARNING("Failed to insert device_private_data for device (%p) as host is out of memory",
                      reinterpret_cast<void *>(dev));

      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
}

void device_private_data::disassociate(VkDevice dev)
{
   assert(dev != VK_NULL_HANDLE);
   device_private_data *device_data = nullptr;
   {
      scoped_mutex lock(g_data_lock);
      auto it = g_device_data.find(get_key(dev));
      if (it == g_device_data.end())
      {
         WSI_LOG_WARNING("Failed to find private data for device (%p)", reinterpret_cast<void *>(dev));
         return;
      }

      device_data = it->second;
      g_device_data.erase(it);
   }

   destroy(device_data);
}

template <typename dispatchable_type>
static device_private_data &get_device_private_data(dispatchable_type dispatchable_object)
{
   scoped_mutex lock(g_data_lock);

   return *g_device_data.at(get_key(dispatchable_object));
}

device_private_data &device_private_data::get(VkDevice device)
{
   return get_device_private_data(device);
}

device_private_data &device_private_data::get(VkQueue queue)
{
   return get_device_private_data(queue);
}

VkResult device_private_data::add_layer_swapchain(VkSwapchainKHR swapchain)
{
   scoped_mutex lock(swapchains_lock);
   auto result = swapchains.try_insert(swapchain);
   return result.has_value() ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

bool device_private_data::layer_owns_all_swapchains(const VkSwapchainKHR *swapchain, uint32_t swapchain_count) const
{
   scoped_mutex lock(swapchains_lock);
   for (uint32_t i = 0; i < swapchain_count; i++)
   {
      if (swapchains.find(swapchain[i]) == swapchains.end())
      {
         return false;
      }
   }
   return true;
}

bool device_private_data::should_layer_create_swapchain(VkSurfaceKHR vk_surface)
{
   return instance_data.should_layer_handle_surface(physical_device, vk_surface);
}

bool device_private_data::can_icds_create_swapchain(VkSurfaceKHR vk_surface)
{
   return disp.CreateSwapchainKHR != nullptr;
}

VkResult device_private_data::set_device_enabled_extensions(const char *const *extension_names, size_t extension_count)
{
   return enabled_extensions.add(extension_names, extension_count, supported_device_extensions.data(),
                                 supported_device_extensions.size());
}

bool device_private_data::is_device_extension_enabled(const char *extension_name) const
{
   return enabled_extensions.contains(extension_name);
}

void device_private_data::destroy(device_private_data *device_data)
{
   assert(device_data);

   auto alloc = device_data->get_allocator();
   alloc.destroy<device_private_data>(1, device_data);
}

} /* namespace layer */
