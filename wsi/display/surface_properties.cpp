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

#include <cstring>
#include <algorithm>

#include "surface_properties.hpp"
#include "util/macros.hpp"

namespace wsi
{

namespace display
{

constexpr int max_core_1_0_formats = VK_FORMAT_ASTC_12x12_SRGB_BLOCK + 1;

VkResult surface_properties::get_surface_capabilities(VkPhysicalDevice physical_device,
                                                      VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   get_surface_capabilities_common(physical_device, pSurfaceCapabilities);

   /* Image count limits */
   pSurfaceCapabilities->minImageCount = 2;
   pSurfaceCapabilities->maxImageCount = 3;

   /* Composite alpha */
   pSurfaceCapabilities->supportedCompositeAlpha =
      static_cast<VkCompositeAlphaFlagBitsKHR>(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);

   return VK_SUCCESS;
}

VkResult surface_properties::get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surfaceFormatCount,
                                                 VkSurfaceFormatKHR *surfaceFormats,
                                                 VkSurfaceFormat2KHR *extended_surface_formats)
{
   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   int drm_fd = display->get_drm_fd();
   if (drm_fd == -1)
   {
      return VK_ERROR_SURFACE_LOST_KHR;
   }
   /* Allow userspace to query native primary plane information */
   if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
   {
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   drm_plane_resources_owner plane_res{ drmModeGetPlaneResources(drm_fd) };
   if (plane_res == nullptr || plane_res->count_planes == 0)
   {
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   /* Look for the primary plane */
   drm_plane_owner plane{ nullptr };
   for (uint32_t i = 0; i < plane_res->count_planes; i++)
   {
      drm_plane_owner temp_plane{ drmModeGetPlane(drm_fd, plane_res->planes[i]) };
      if (temp_plane != nullptr)
      {
         drm_object_properties_owner props{ drmModeObjectGetProperties(drm_fd, plane_res->planes[i],
                                                                       DRM_MODE_OBJECT_PLANE) };
         if (props != nullptr)
         {
            auto props_end = props->props + props->count_props;
            auto prop = std::find_if(props->props, props_end, [drm_fd](auto &property_id) {
               drm_property_owner prop{ drmModeGetProperty(drm_fd, property_id) };
               if (prop != nullptr && !strcmp(prop->name, "type"))
               {
                  return true;
               }
               return false;
            });
            if (prop != props_end)
            {
               auto index = std::distance(props->props, prop);
               if (props->prop_values[index] == DRM_PLANE_TYPE_PRIMARY)
               {
                  plane = std::move(temp_plane);
                  break;
               }
            }
         }
      }
   }

   if (plane == nullptr)
   {
      WSI_LOG_ERROR("Failed to find primary plane.");
      return VK_ERROR_SURFACE_LOST_KHR;
   }

   uint32_t format_count = 0;

   /* If this happens, it is just wrong */
   assert(plane->count_formats > 0);
   assert(plane->count_formats <= max_core_1_0_formats);

   std::array<surface_format_properties, max_core_1_0_formats> formats{};

   for (uint32_t i = 0; i < plane->count_formats; ++i)
   {
      auto vk_format = util::drm::drm_to_vk_format(plane->formats[i]);
      if (VK_FORMAT_UNDEFINED != vk_format)
      {
         formats[format_count] = surface_format_properties{ vk_format };
         VkPhysicalDeviceImageFormatInfo2KHR format_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR,
                                                             nullptr,
                                                             vk_format,
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                             0 };
         VkResult res = formats[format_count].check_device_support(physical_device, format_info);
         if (VK_SUCCESS == res)
         {
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
            if (layer::instance_private_data::get(physical_device).has_image_compression_support(physical_device))
            {
               formats[format_count].add_device_compression_support(physical_device, format_info);
            }
#endif
            format_count++;
         }
      }

      /* Certain 8-bit UNORM formats can be interpreted as both UNORM and sRGB by Vulkan, so expose both formats.
       * The colorSpace value is how the presentation engine interprets the format.
       * The linearity of VkFormat and the display format may be different.
       */
      auto vk_srgb_format = util::drm::drm_to_vk_srgb_format(plane->formats[i]);
      if (VK_FORMAT_UNDEFINED != vk_srgb_format)
      {
         formats[format_count] = surface_format_properties{ vk_srgb_format };
         VkPhysicalDeviceImageFormatInfo2KHR format_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR,
                                                             nullptr,
                                                             vk_format,
                                                             VK_IMAGE_TYPE_2D,
                                                             VK_IMAGE_TILING_OPTIMAL,
                                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                             0 };
         VkResult res = formats[format_count].check_device_support(physical_device, format_info);
         if (VK_SUCCESS == res)
         {
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
            if (layer::instance_private_data::get(physical_device).has_image_compression_support(physical_device))
            {
               formats[format_count].add_device_compression_support(physical_device, format_info);
            }
#endif
            format_count++;
         }
      }
   }

   return surface_properties_formats_helper(formats.begin(), formats.begin() + format_count, surfaceFormatCount,
                                            surfaceFormats, extended_surface_formats);
}

VkResult surface_properties::get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                                       uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes)
{
   UNUSED(physical_device);
   UNUSED(surface);

   static const std::array<VkPresentModeKHR, 1> modes = { VK_PRESENT_MODE_FIFO_KHR };

   return get_surface_present_modes_common(pPresentModeCount, pPresentModes, modes);
}

VWL_VKAPI_CALL(VkResult)
CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display,
                     const VkDisplayModeCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                     VkDisplayModeKHR *pMode)
{
   UNUSED(physicalDevice);
   UNUSED(pAllocator);

   assert(display != VK_NULL_HANDLE);
   assert(pMode != nullptr);

   assert(pCreateInfo != nullptr);
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR);
   assert(pCreateInfo->pNext == NULL);
   assert(pCreateInfo->flags == 0);

   drm_display *dpy = reinterpret_cast<drm_display *>(display);

   const VkDisplayModeParametersKHR *params = &pCreateInfo->parameters;

   if (params->visibleRegion.width == 0 || params->visibleRegion.height == 0 || params->refreshRate == 0)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto *mode = std::find_if(dpy->get_display_modes_begin(), dpy->get_display_modes_end(), [params](auto &mode) {
      return mode.get_width() == params->visibleRegion.width && mode.get_height() == params->visibleRegion.height &&
             mode.get_refresh_rate() == params->refreshRate;
   });

   if (mode != dpy->get_display_modes_end())
   {
      *pMode = reinterpret_cast<VkDisplayModeKHR>(mode);

      return VK_SUCCESS;
   }

   return VK_ERROR_INITIALIZATION_FAILED;
}

VWL_VKAPI_CALL(VkResult)
CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
   UNUSED(instance);
   UNUSED(pCreateInfo);
   UNUSED(pAllocator);
   UNUSED(pSurface);
   // TODO: Create the surface object here, which in turn creates the surface_properties object

   return VK_ERROR_EXTENSION_NOT_PRESENT;
}

VWL_VKAPI_CALL(VkResult)
GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t *pPropertyCount,
                            VkDisplayModePropertiesKHR *pProperties)
{
   UNUSED(physicalDevice);
   assert(display != VK_NULL_HANDLE);
   assert(pPropertyCount != nullptr);

   drm_display *dpy = reinterpret_cast<drm_display *>(display);
   assert(dpy != nullptr);

   drm_display_mode *modes{ dpy->get_display_modes_begin() };
   size_t num_modes{ dpy->get_num_display_modes() };

   if (pProperties == nullptr)
   {
      *pPropertyCount = num_modes;
      return VK_SUCCESS;
   }

   uint32_t nr_properties = std::min(*pPropertyCount, static_cast<uint32_t>(num_modes));
   *pPropertyCount = 0;
   std::for_each(modes, modes + nr_properties, [&pProperties, &pPropertyCount](auto &mode) {
      VkDisplayModePropertiesKHR properties = {};

      VkDisplayModeKHR display_mode = reinterpret_cast<VkDisplayModeKHR>(&mode);
      properties.displayMode = display_mode;

      VkDisplayModeParametersKHR parameters{};
      parameters.visibleRegion = { mode.get_width(), mode.get_height() };
      parameters.refreshRate = mode.get_refresh_rate();
      properties.parameters = parameters;

      pProperties[*pPropertyCount] = properties;
      *pPropertyCount += 1;
   });

   if (*pPropertyCount < static_cast<uint32_t>(num_modes))
   {
      return VK_INCOMPLETE;
   }

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex,
                               VkDisplayPlaneCapabilitiesKHR *pCapabilities)
{
   assert(physicalDevice != VK_NULL_HANDLE);
   assert(mode != VK_NULL_HANDLE);
   assert(pCapabilities != nullptr);

   drm_display_mode *display_mode = reinterpret_cast<drm_display_mode *>(mode);
   assert(display_mode != nullptr);

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      WSI_LOG_ERROR("DRM display not available.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* Implementation supports only one plane for presenting
    * images. Therefore plane index must be 0. */
   assert(planeIndex == 0);

   auto valid_mode =
      std::find_if(display->get_display_modes_begin(), display->get_display_modes_end(), [&display_mode](auto &mode) {
         return (display_mode->get_width() == mode.get_width()) && (display_mode->get_height() == mode.get_height()) &&
                (display_mode->get_refresh_rate() == mode.get_refresh_rate());
      });

   assert(valid_mode != display->get_display_modes_end());

   VkDisplayPlaneCapabilitiesKHR planeCapabilities{};
   planeCapabilities.supportedAlpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
   planeCapabilities.minSrcPosition = { 0, 0 };
   planeCapabilities.maxSrcPosition = { 0, 0 };
   /* Implementation allows swapchains to be a subset of the display area. */
   planeCapabilities.minSrcExtent = { 0, 0 };
   planeCapabilities.maxSrcExtent = { display_mode->get_width(), display_mode->get_height() };
   planeCapabilities.minDstPosition = { 0, 0 };
   planeCapabilities.maxDstPosition = { 0, 0 };
   planeCapabilities.minDstExtent = { display_mode->get_width(), display_mode->get_height() };
   planeCapabilities.maxDstExtent = { display_mode->get_width(), display_mode->get_height() };

   *pCapabilities = planeCapabilities;

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t *pDisplayCount,
                                    VkDisplayKHR *pDisplays)
{
   assert(physicalDevice != VK_NULL_HANDLE);
   assert(pDisplayCount != nullptr);

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      WSI_LOG_ERROR("DRM display not available.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* Implementation supports only one plane for presenting
    * images. Therefore plane index must be 0. */
   assert(planeIndex == 0);

   if (pDisplays == nullptr)
   {
      /* Implementation will expose just one (the main)
       * plane for the application to use. */
      *pDisplayCount = 1;
      return VK_SUCCESS;
   }

   if (*pDisplayCount == 0)
   {
      return VK_INCOMPLETE;
   }

   *pDisplays = reinterpret_cast<VkDisplayKHR>(&display.value());
   *pDisplayCount = 1;

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                           VkDisplayPlanePropertiesKHR *pProperties)
{
   assert(physicalDevice != VK_NULL_HANDLE);
   assert(pPropertyCount != nullptr);

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      WSI_LOG_ERROR("DRM display not available.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (pProperties == nullptr)
   {
      /* Implementation will expose just one (the main)
       * plane for the application to use. */
      *pPropertyCount = 1;
      return VK_SUCCESS;
   }

   if (*pPropertyCount == 0)
   {
      return VK_INCOMPLETE;
   }

   VkDisplayPlanePropertiesKHR planeProperties{};
   planeProperties.currentDisplay = reinterpret_cast<VkDisplayKHR>(&display.value());

   /* Since the implementation is exposing just one plane the value for
    * the current stack index must be 0.*/
   planeProperties.currentStackIndex = 0;

   *pProperties = planeProperties;
   *pPropertyCount = 1;

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                      VkDisplayPropertiesKHR *pProperties)
{
   assert(physicalDevice != VK_NULL_HANDLE);
   assert(pPropertyCount != nullptr);

   auto &display = drm_display::get_display();

   if (!display.has_value())
   {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   if (pProperties == nullptr)
   {
      *pPropertyCount = 1;
      return VK_SUCCESS;
   }

   if (*pPropertyCount == 0)
   {
      return VK_INCOMPLETE;
   }

   *pPropertyCount = 1;

   VkDisplayPropertiesKHR display_properties = {};
   display_properties.display = reinterpret_cast<VkDisplayKHR>(&display.value());
   display_properties.displayName = "DRM display";
   display_properties.physicalDimensions = { display->get_connector()->mmWidth, display->get_connector()->mmHeight };
   display_properties.physicalResolution = { display->get_max_width(), display->get_max_height() };
   display_properties.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   display_properties.planeReorderPossible = VK_FALSE;
   display_properties.persistentContent = VK_FALSE;

   *pProperties = display_properties;

   return VK_SUCCESS;
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{

   if (strcmp(name, "vkCreateDisplayModeKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateDisplayModeKHR);
   }
   else if (strcmp(name, "vkCreateDisplayPlaneSurfaceKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateDisplayPlaneSurfaceKHR);
   }
   else if (strcmp(name, "vkGetDisplayModePropertiesKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetDisplayModePropertiesKHR);
   }
   else if (strcmp(name, "vkGetDisplayPlaneCapabilitiesKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetDisplayPlaneCapabilitiesKHR);
   }
   else if (strcmp(name, "vkGetDisplayPlaneSupportedDisplaysKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetDisplayPlaneSupportedDisplaysKHR);
   }
   else if (strcmp(name, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceDisplayPlanePropertiesKHR);
   }
   else if (strcmp(name, "vkGetPhysicalDeviceDisplayPropertiesKHR") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceDisplayPropertiesKHR);
   }

   return nullptr;
}

VkResult surface_properties::get_required_instance_extensions(util::extension_list &extension_list)
{
   const std::array required_instance_extensions{
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
   };

   return extension_list.add(required_instance_extensions.data(), required_instance_extensions.size());
}

bool surface_properties::is_surface_extension_enabled(const layer::instance_private_data &instance_data)
{
   return instance_data.is_instance_extension_enabled(VK_KHR_SURFACE_EXTENSION_NAME);
}

surface_properties &surface_properties::get_instance()
{
   static surface_properties instance;

   return instance;
}

} /* namespace display */

} /* namespace wsi */