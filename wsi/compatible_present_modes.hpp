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
 * @file compatible_present_modes.hpp
 *
 * @brief Contains functions for handling compatibility between different presentation modes
 */

#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <util/log.hpp>

namespace wsi
{

static constexpr uint32_t MAX_PRESENT_MODES = 6;
struct present_mode_compatibility
{
   /* Presentation mode */
   VkPresentModeKHR present_mode;

   /* Number of presentation modes compatible */
   uint32_t present_mode_count;

   /* Stores the compatible presentation modes */
   std::array<VkPresentModeKHR, MAX_PRESENT_MODES> compatible_present_modes;
};

template <std::size_t SIZE>
class compatible_present_modes
{
public:
   compatible_present_modes()
   {
   }

   compatible_present_modes(std::array<present_mode_compatibility, SIZE> present_mode_compatibilites)
      : m_present_mode_compatibilites(present_mode_compatibilites)
   {
   }

   /**
    * @brief Common function for handling VkSurfacePresentModeCompatibilityEXT if it exists in the pNext chain of VkSurfaceCapabilities2KHR.
    *
    * If pSurfaceInfo contains a VkSurfacePresentModeEXT struct in its pNext chain, and pSurfaceCapabilities contains a VkSurfacePresentModeCompatibilityEXT struct
    * then this function fills the VkSurfacePresentModeCompatibilityEXT struct with the presentation modes that are compatible with the presentation mode specified
    * in the VkSurfacePresentModeEXT struct.
    *
    * @param pSurfaceInfo                 Pointer to surface info that may or may not contain a VkSurfacePresentModeEXT.
    * @param pSurfaceCapabilities         Pointer to surface capabilities that may or may not contain a VkSurfacePresentModeCompatibilityEXT struct.
    *
    */
   void get_surface_present_mode_compatibility_common(const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                      VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
   {
      auto surface_present_mode =
         util::find_extension<VkSurfacePresentModeEXT>(VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT, pSurfaceInfo);
      auto surface_present_mode_compatibility = util::find_extension<VkSurfacePresentModeCompatibilityEXT>(
         VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT, pSurfaceCapabilities);

      if (surface_present_mode == nullptr || surface_present_mode_compatibility == nullptr)
      {
         return;
      }

      VkPresentModeKHR present_mode = surface_present_mode->presentMode;
      auto it = std::find_if(m_present_mode_compatibilites.begin(), m_present_mode_compatibilites.end(),
                             [present_mode](present_mode_compatibility p) { return p.present_mode == present_mode; });
      if (it == m_present_mode_compatibilites.end())
      {
         WSI_LOG_ERROR("Querying compatible presentation mode support for a presentation mode that is not supported.");
         return;
      }
      const present_mode_compatibility &surface_supported_compatibility = *it;

      if (surface_present_mode_compatibility->pPresentModes == nullptr)
      {
         surface_present_mode_compatibility->presentModeCount = surface_supported_compatibility.present_mode_count;
         return;
      }

      surface_present_mode_compatibility->presentModeCount = std::min(
         surface_present_mode_compatibility->presentModeCount, surface_supported_compatibility.present_mode_count);
      std::copy(surface_supported_compatibility.compatible_present_modes.begin(),
                surface_supported_compatibility.compatible_present_modes.begin() +
                   surface_present_mode_compatibility->presentModeCount,
                surface_present_mode_compatibility->pPresentModes);
   }

   /**
    * @brief Common function for handling checking whether a present mode is compatible with another.
    *
    * @param present_mode_a                 First present mode.
    * @param present_mode_b                 Second present mode to compare against.
    *
    * @return true if compatible, false otherwise.
    */
   bool is_compatible_present_modes(VkPresentModeKHR present_mode_a, VkPresentModeKHR present_mode_b)
   {
      auto it =
         std::find_if(m_present_mode_compatibilites.begin(), m_present_mode_compatibilites.end(),
                      [present_mode_a](present_mode_compatibility p) { return p.present_mode == present_mode_a; });
      if (it == m_present_mode_compatibilites.end())
      {
         WSI_LOG_ERROR("Querying compatible presentation mode support for a presentation mode that is not supported.");
         return false;
      }

      const present_mode_compatibility &present_mode_comp = *it;
      auto present_mode_it =
         std::find_if(present_mode_comp.compatible_present_modes.begin(),
                      present_mode_comp.compatible_present_modes.begin() + present_mode_comp.present_mode_count,
                      [present_mode_b](VkPresentModeKHR p) { return p == present_mode_b; });
      return present_mode_it != present_mode_comp.compatible_present_modes.end();
   }

private:
   std::array<present_mode_compatibility, SIZE> m_present_mode_compatibilites;
};

} // namespace wsi
