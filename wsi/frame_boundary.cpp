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
 * @file frame_boundary.cpp
 *
 * @brief Contains the functionality for frame boundary handling.
 */

#include "frame_boundary.hpp"

namespace wsi
{
frame_boundary_handler::frame_boundary_handler(const layer::device_private_data &device_data)
   : m_handle_frame_boundary_events(device_data.should_layer_handle_frame_boundary_events())
{
}

std::optional<VkFrameBoundaryEXT> frame_boundary_handler::handle_frame_boundary_event(
   const VkPresentInfoKHR *present_info, VkImage *current_image_to_be_presented)
{
   /* If frame boundary feature is not enabled by the application, the layer will pass its own frame boundary events back to ICD.
    * Otherwise, let the application handle the frame boundary events. */
   return m_handle_frame_boundary_events ? create_frame_boundary(current_image_to_be_presented) :
                                           wsi::create_frame_boundary(*present_info);
}

std::optional<VkFrameBoundaryEXT> create_frame_boundary(const VkPresentInfoKHR &present_info)
{
   auto *present_frame_boundary =
      util::find_extension<VkFrameBoundaryEXT>(VK_STRUCTURE_TYPE_PRESENT_ID_KHR, present_info.pNext);

   /* Extract the VkFrameBoundaryEXT structure to avoid passing other, unrelated structures to vkQueueSubmit */
   if (present_frame_boundary != nullptr)
   {
      return util::shallow_copy_extension(present_frame_boundary);
   }

   return std::nullopt;
}

bool frame_boundary_handler::should_layer_handle_frame_boundary_events() const
{
   return m_handle_frame_boundary_events;
}

VkFrameBoundaryEXT frame_boundary_handler::create_frame_boundary(VkImage *image)
{
   VkFrameBoundaryEXT frame_boundary{};
   frame_boundary.pNext = nullptr;
   frame_boundary.sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT;
   frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
   /* Number of presented images by swapchain as the frame boundary
    * would not work as when the page flip thread is running, the
    * number frame ID could remain the same until the image is picked
    * up by the thread so we use our own counter for the frame boundary. */
   frame_boundary.frameID = m_current_frame_boundary_id++;
   frame_boundary.imageCount = 1;
   frame_boundary.pImages = image;
   frame_boundary.pBuffers = nullptr;
   frame_boundary.bufferCount = 0;

   /* Create an unique identifier for the layer in case tools make use of it.
    * The number below is derived from converting characters 'WSI' into
    * their numerical representation from the ASCII table. */
   frame_boundary.tagName = 0x575349;

   /* No additional data attached */
   frame_boundary.tagSize = 0;
   frame_boundary.pTag = nullptr;

   return frame_boundary;
}

}