/*
 * Copyright (c) 2017-2019, 2021-2022 Arm Limited.
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

#include "wsi/surface_properties.hpp"
#include "util/unordered_set.hpp"

namespace wsi
{
namespace x11
{

class surface;

class surface_properties : public wsi::surface_properties
{
public:
   surface_properties(surface &wsi_surface, const util::allocator &alloc);

   static surface_properties &get_instance();

   VkResult get_surface_capabilities(VkPhysicalDevice physical_device,
                                     VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) override;
   VkResult get_surface_formats(VkPhysicalDevice physical_device, uint32_t *surfaceFormatCount,
                                VkSurfaceFormatKHR *surfaceFormats,
                                VkSurfaceFormat2KHR *extended_surface_formats) override;
   VkResult get_surface_present_modes(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                      uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) override;

   PFN_vkVoidFunction get_proc_addr(const char *name) override;

   bool is_surface_extension_enabled(const layer::instance_private_data &instance_data) override;

   VkResult get_required_device_extensions(util::extension_list &extension_list) override;

private:
   surface_properties();

   /** If the properties are specific to a @ref wsi::wayland::surface this is a pointer to it. Can be nullptr for
    * generic Wayland surface properties.
    */
   surface *specific_surface;
};

} // namespace x11
} // namespace wsi
