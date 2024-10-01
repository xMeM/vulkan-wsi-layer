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
 * @file frame_boundary.hpp
 *
 * @brief Contains the functionality for frame boundary handling.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <layer/private_data.hpp>

#include <optional>

namespace wsi
{
class frame_boundary_handler
{
public:
   frame_boundary_handler(const layer::device_private_data &device_data);

   /**
    * @brief Handle frame boundary event at present time
    *
    * @param present_info Information about the swapchain and image to be presented.
    * @param current_image_to_be_presented Address to the currently to be presented image
    */
   std::optional<VkFrameBoundaryEXT> handle_frame_boundary_event(const VkPresentInfoKHR *present_info,
                                                                 VkImage *current_image_to_be_presented);

private:
   /**
    * @brief Create a frame boundary with the current image
    *
    * @param image Address to the currently to be presented image
    * @return Frame boundary structure
    */
   VkFrameBoundaryEXT create_frame_boundary(VkImage *image);

   /**
    * @brief Check whether we should handle frame boundary events.
    *
    * @return true if supported, false otherwise.
    */
   bool should_layer_handle_frame_boundary_events() const;

   /**
    * @brief Holds the number of the current frame identifier for the swapchain
    */
   uint64_t m_current_frame_boundary_id{ 0 };

   /**
    * @brief Stores whether the layer should handle frame boundary events.
    */
   const bool m_handle_frame_boundary_events;
};

/**
 * @brief Create a frame boundary object
 *
 * @param present_info Present info
 * @return Frame boundary if @p present_info has passed it.
 */
std::optional<VkFrameBoundaryEXT> create_frame_boundary(const VkPresentInfoKHR &present_info);

}
