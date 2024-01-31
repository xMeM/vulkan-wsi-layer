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

#pragma once

#include <vulkan/vulkan.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <optional>

#include "util/custom_allocator.hpp"
#include "util/file_descriptor.hpp"

namespace wsi
{

namespace display
{

/* Owner types for DRM API objects */
template <typename T, void (*drm_object_free)(T *)>
struct drm_deleter
{
   void operator()(T *object)
   {
      if (object != nullptr)
      {
         drm_object_free(object);
      }
   }
};

template <typename T, void (*drm_object_free)(T *)>
using drm_owner = std::unique_ptr<T, drm_deleter<T, drm_object_free>>;
using drm_resources_owner = drm_owner<_drmModeRes, drmModeFreeResources>;
using drm_connector_owner = drm_owner<_drmModeConnector, drmModeFreeConnector>;
using drm_encoder_owner = drm_owner<_drmModeEncoder, drmModeFreeEncoder>;
using drm_plane_owner = drm_owner<_drmModePlane, drmModeFreePlane>;
using drm_plane_resources_owner = drm_owner<_drmModePlaneRes, drmModeFreePlaneResources>;
using drm_object_properties_owner = drm_owner<_drmModeObjectProperties, drmModeFreeObjectProperties>;
using drm_property_owner = drm_owner<_drmModeProperty, drmModeFreeProperty>;

/* Forward declaration */
class drm_display;

/**
 * @brief The display mode object.
 * The drm_display_mode class stores information
 * about a drm mode.
 */
class drm_display_mode
{
public:
   /**
    * @brief drm_display_mode default constructor.
    */
   drm_display_mode();

   /**
    * @brief Get the width of the display mode.
    *
    * @return Width of the display mode.
    */
   uint16_t get_width() const;

   /**
    * @brief Get the height of the display mode.
    *
    * @return Height of the display mode.
    */
   uint16_t get_height() const;

   /**
    * @brief Get the display mode refresh rate.
    *
    * @return Refresh rate of the display mode.
    */
   uint32_t get_refresh_rate() const;

   /**
    * @brief get function for display instance.
    *
    * @return The display instance.
    */
   const drm_display *get_display();

   /**
    * @brief Sets the display instance.
    *
    * @param disp The display instance.
    */
   void set_display(const drm_display *disp);

   /**
    * @brief Get function for the drm mode.
    */
   drmModeModeInfo get_drm_mode() const;

   /**
    * @brief Sets the drm mode info.
    *
    * @param mode The drm mode.
    */
   void set_drm_mode(drmModeModeInfo mode);

   /**
    * @brief Check if this mode is the preferred mode for the connector.
    *
    * @return true if the mode is the preferred mode, otherwise false.
    */
   bool is_preferred() const;

   /**
    * @brief Set the preferred state.
    *
    * @param preferred The state to set.
    */
   void set_preferred(bool preferred);

private:
   /**
    * @brief the display instance.
    */
   const drm_display *m_display;

   /**
    * @brief Cached native drm mode.
    */
   drmModeModeInfo m_drm_mode_info;

   /**
    * @brief Flag for the preferred display mode.
    */
   bool m_preferred = false;
};

/**
 * @brief The vulkan's display object.
 * The display class wraps a VkDisplayKHR.
 */
class drm_display
{
public:
   /**
    * @brief Construct and initialize a display object.
    *
    * @param allocator The allocator object that the display will use.
    * @return std::optional<drm_display> containing a display if initialization went well, otherwise std::nullopt.
    */
   static std::optional<util::unique_ptr<drm_display>> make_display(const util::allocator &allocator,
                                                                    const char *drm_device);

   /**
    * @brief display destructor.
    */
   ~drm_display();

   /**
    * @brief Get the display modes begin pointer.
    *
    * @return drm_display_mode* pointer to the first display mode.
    */
   drm_display_mode *get_display_modes_begin() const;

   /**
    * @brief Get the display modes end pointer.
    *
    * @return drm_display_mode* pointer past the last display mode.
    */
   drm_display_mode *get_display_modes_end() const;

   /**
    * @brief Get number of display modes.
    *
    * @return const size_t number of display modes.
    */
   size_t get_num_display_modes() const;

   /**
    * @brief Get function for drm device file descriptor.
    *
    * @return The display device file descriptor.
    */
   int get_drm_fd() const;

   /**
    * @brief Get function for the connector id.
    *
    * @return The connector id.
    */
   uint32_t get_connector_id() const;

   /**
    * @brief Get connector corresponding to the current connector id.
    *
    * @return The connector.
    */
   drmModeConnector *get_connector() const;

   /**
    * @brief Get drm resources.
    *
    * @return The drm resources pointer.
    */
   drmModeResPtr get_drm_resources() const;

   /**
    * @brief Returns a CRTC compatible with this display's connector.
    *
    * @return The CRTC id.
    */
   int get_crtc_id() const;

   /**
    * @brief Get the max width of the display in pixels.
    */
   uint32_t get_max_width() const;

   /**
    * @brief Get the max height of the display in pixels.
    */
   uint32_t get_max_height() const;

private:
   /* Allow util::allocator to access the private constructor */
   friend class util::allocator;

   /**
    * @brief display constructor.
    *
    * @param allocator The allocator that the display will use.
    */
   drm_display(util::fd_owner drm_fd, int crtc_id, drm_connector_owner drm_connector,
               util::unique_ptr<drm_display_mode> display_modes, size_t num_display_modes, uint32_t max_width,
               uint32_t max_height);

   /**
    * @brief File descriptor for the display device.
    */
   util::fd_owner m_drm_fd;

   /**
    * @brief Id of CRTC compatible with the chosen connector.
    */
   int m_crtc_id;

   /**
    * @brief Handle to the drm connector.
    */
   drm_connector_owner m_drm_connector;

   /**
    * @brief Pointer to available display modes for the connected display.
    */
   util::unique_ptr<drm_display_mode> m_display_modes;

   /**
    * @brief Number of available display modes in @ref m_display_modes.
    */
   size_t m_num_display_modes;

   /**
    * @brief Maximum display resolution width.
    */
   uint32_t m_max_width;

   /**
    * @brief Maximum display resolution height.
    */
   uint32_t m_max_height;
};

} /* namespace display */

} /* namespace wsi */
