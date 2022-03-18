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

#include "format_modifiers.hpp"
#include "layer/private_data.hpp"

namespace util
{

VkResult get_drm_format_properties(VkPhysicalDevice physical_device, VkFormat format,
                                   util::vector<VkDrmFormatModifierPropertiesEXT> &format_props_list)
{
   auto &instance_data = layer::instance_private_data::get(physical_device);

   VkDrmFormatModifierPropertiesListEXT format_modifier_props = {};
   format_modifier_props.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

   VkFormatProperties2KHR format_props = {};
   format_props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR;
   format_props.pNext = &format_modifier_props;

   instance_data.disp.GetPhysicalDeviceFormatProperties2KHR(physical_device, format, &format_props);

   if (!format_props_list.try_resize(format_modifier_props.drmFormatModifierCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   format_modifier_props.pDrmFormatModifierProperties = format_props_list.data();
   instance_data.disp.GetPhysicalDeviceFormatProperties2KHR(physical_device, format, &format_props);
   return VK_SUCCESS;
}
}  /* namespace util */
