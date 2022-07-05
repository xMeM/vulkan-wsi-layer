/*
 * Copyright (c) 2022 Arm Limited.
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

#include "surface_properties.hpp"
#include "layer/private_data.hpp"

namespace wsi
{

VkResult surface_format_properties::check_device_support(VkPhysicalDevice phys_dev,
                                                         VkPhysicalDeviceImageFormatInfo2KHR image_format_info)
{
   VkImageFormatProperties2KHR image_format_props{ VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR, nullptr };

   auto &instance_data = layer::instance_private_data::get(phys_dev);

   return instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(phys_dev, &image_format_info,
                                                                        &image_format_props);
}

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
VkResult surface_format_properties::add_device_compression_support(
   VkPhysicalDevice phys_dev, VkPhysicalDeviceImageFormatInfo2KHR image_format_info)
{
   auto &instance_data = layer::instance_private_data::get(phys_dev);

   VkImageCompressionPropertiesEXT compression_props = { VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT, nullptr, 0,
                                                         0 };
   VkImageFormatProperties2KHR image_format_props{ VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR,
                                                   &compression_props };

   VkImageCompressionControlEXT compression_control{ VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT,
                                                     image_format_info.pNext,
                                                     VK_IMAGE_COMPRESSION_FIXED_RATE_DEFAULT_EXT };
   image_format_info.pNext = &compression_control;

   VkResult res =
      instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(phys_dev, &image_format_info, &image_format_props);
   if (res == VK_SUCCESS)
   {
      m_compression.imageCompressionFlags |= compression_props.imageCompressionFlags;
      m_compression.imageCompressionFixedRateFlags |= compression_props.imageCompressionFixedRateFlags;
   }
   else if (res != VK_ERROR_FORMAT_NOT_SUPPORTED)
   {
      return res;
   }

   return VK_SUCCESS;
}
#endif

void surface_format_properties::fill_format_properties(VkSurfaceFormat2KHR &surf_format) const
{
   surf_format.surfaceFormat = m_surface_format;
#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   auto *compression_properties = util::find_extension<VkImageCompressionPropertiesEXT>(
      VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT, surf_format.pNext);
   if (compression_properties != nullptr)
   {
      /** While a format can support multiple compression control flags the returned value is only allowed to be:
       * VK_IMAGE_COMPRESSION_DEFAULT_EXT, VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT or
       * VK_IMAGE_COMPRESSION_DISABLED_EXT.
       *
       * Since currently formats that are supported with both default and disabled compression are not distinguished
       * from formats that would always be with disabled compression, disabled is not returned.
       */
      compression_properties->imageCompressionFlags = VK_IMAGE_COMPRESSION_DEFAULT_EXT;
      if (m_compression.imageCompressionFlags & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT)
      {
         compression_properties->imageCompressionFlags = VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT;
         compression_properties->imageCompressionFixedRateFlags = m_compression.imageCompressionFixedRateFlags;
      }
   }
#endif
}

void get_surface_capabilities_common(VkPhysicalDevice physical_device, VkSurfaceCapabilitiesKHR *surface_capabilities)
{
   /* Image count limits */
   surface_capabilities->minImageCount = 1;
   surface_capabilities->maxImageCount = surface_properties::MAX_SWAPCHAIN_IMAGE_COUNT;

   /* Surface extents */
   surface_capabilities->currentExtent = { 0xffffffff, 0xffffffff };
   surface_capabilities->minImageExtent = { 1, 1 };
   /* Ask the device for max */
   VkPhysicalDeviceProperties dev_props;
   layer::instance_private_data::get(physical_device).disp.GetPhysicalDeviceProperties(physical_device, &dev_props);

   surface_capabilities->maxImageExtent = { dev_props.limits.maxImageDimension2D,
                                            dev_props.limits.maxImageDimension2D };
   surface_capabilities->maxImageArrayLayers = 1;

   /* Surface transforms */
   surface_capabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   surface_capabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

   /* Composite alpha */
   surface_capabilities->supportedCompositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);

   /* Image usage flags */
   surface_capabilities->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
}

} /* namespace wsi */