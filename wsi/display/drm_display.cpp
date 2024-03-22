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

#include "drm_display.hpp"
#include "util/custom_allocator.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <mutex>
namespace wsi
{

namespace display
{

const std::string default_dri_device_name{ "/dev/dri/card0" };

drm_display::drm_display(util::fd_owner drm_fd, int crtc_id, drm_connector_owner drm_connector,
                         util::unique_ptr<drm_display_mode> display_modes, size_t num_display_modes, uint32_t max_width,
                         uint32_t max_height)
   : m_drm_fd(std::move(drm_fd))
   , m_crtc_id(crtc_id)
   , m_drm_connector(std::move(drm_connector))
   , m_display_modes(std::move(display_modes))
   , m_num_display_modes(num_display_modes)
   , m_max_width(max_width)
   , m_max_height(max_height)
{
}

drm_display::~drm_display()
{
   if (m_drm_fd.is_valid())
   {
      /* Finish using the DRM device. */
      drmDropMaster(m_drm_fd.get());
   }
}

/**
 * @brief Utility function to find a compatible CRTC to drive this display's connector.
 *
 * @return An integer < 0 on failure, otherwise a valid CRTC id.
 */
static int find_compatible_crtc(int fd, drm_resources_owner &resources, drm_connector_owner &connector)
{
   assert(resources);
   assert(connector);

   for (int i = 0; i < connector->count_encoders; i++)
   {
      drm_encoder_owner encoder{ drmModeGetEncoder(fd, connector->encoders[i]) };
      if (!encoder)
      {
         /* Cannot find an encoder, ignore this one. */
         continue;
      }

      /* Iterate over all global CRTCs. */
      for (int j = 0; j < resources->count_crtcs; j++)
      {
         /* Is this encoder compatible with the CRTC? */
         if (!(encoder->possible_crtcs & (1 << j)))
         {
            /* Encoder not compatible, so skip this CRTC. */
            continue;
         }

         /* Make the assumption that only one connector will be in use at a time so there is no
          * need to check that any other connectors are being driven by this CRTC. */
         return resources->crtcs[j];
      }
   }

   WSI_LOG_WARNING("Failed to find compatible CRTC.");

   return -ENODEV;
}

std::optional<drm_display> drm_display::make_display(const util::allocator &allocator, const char *drm_device)
{
   util::fd_owner drm_fd{ open(drm_device, O_RDWR | O_CLOEXEC, 0) };

   if (!drm_fd.is_valid())
   {
      WSI_LOG_ERROR("Failed to open DRM device %s.", drm_device);
      return std::nullopt;
   }

   /* Get the DRM master permission so that mode can be set on the drm device later. */
   if (!drmIsMaster(drm_fd.get()))
   {
      if (drmSetMaster(drm_fd.get()) != 0)
      {
         WSI_LOG_ERROR("Failed to set DRM master: %s.", std::strerror(errno));
         return std::nullopt;
      }
   }

   drm_resources_owner resources{ drmModeGetResources(drm_fd.get()) };
   if (resources == nullptr)
   {
      WSI_LOG_ERROR("Failed to get DRM resources.");
      return std::nullopt;
   }

   drm_connector_owner connector{ nullptr };
   int crtc_id = -1;
   for (int i = 0; i < resources->count_connectors; ++i)
   {
      drm_connector_owner temp_connector{ drmModeGetConnector(drm_fd.get(), resources->connectors[i]) };
      if (temp_connector != nullptr && temp_connector->connection == DRM_MODE_CONNECTED)
      {
         crtc_id = find_compatible_crtc(drm_fd.get(), resources, temp_connector);
         if (crtc_id >= 0)
         {
            connector = std::move(temp_connector);
            break;
         }
      }
   }

   if (connector == nullptr)
   {
      WSI_LOG_ERROR("Failed to find connector for DRM device.");
      return std::nullopt;
   }

   uint32_t max_width = 0;
   uint32_t max_height = 0;
   util::vector<drm_display_mode> display_modes{ allocator };

   for (int j = 0; j < connector->count_modes; ++j)
   {
      /* Need the full drmModeModeInfo cached to supply to drmModeSetCrtc. */
      drm_display_mode mode{};
      mode.set_drm_mode(connector->modes[j]);
      mode.set_preferred(connector->modes[j].type == DRM_MODE_TYPE_PREFERRED);

      uint32_t resolution = static_cast<uint32_t>(mode.get_width()) * static_cast<uint32_t>(mode.get_height());
      if (resolution >= max_width * max_height)
      {
         max_width = mode.get_width();
         max_height = mode.get_height();
      }

      if (!display_modes.try_push_back(mode))
      {
         WSI_LOG_ERROR("Failed to allocate memory for display mode.");
         return std::nullopt;
      }
   }

   util::unique_ptr<drm_display_mode> display_modes_mem{ allocator.create<drm_display_mode>(display_modes.size()) };
   if (display_modes_mem == nullptr)
   {
      WSI_LOG_ERROR("Failed to allocate memory for display mode vector.");
      return std::nullopt;
   }

   std::copy(display_modes.begin(), display_modes.end(), display_modes_mem.get());

   drm_display display{
      std::move(drm_fd), crtc_id,   std::move(connector), std::move(display_modes_mem), display_modes.size(),
      max_width,         max_height
   };

   return std::make_optional(std::move(display));
}

std::optional<drm_display> &drm_display::get_display()
{
   static std::once_flag flag{};
   static std::optional<drm_display> display{ std::nullopt };

   std::call_once(flag, []() {
      const char *dri_device = std::getenv("WSI_DISPLAY_DRI_DEV");
      if (!dri_device)
      {
         dri_device = default_dri_device_name.c_str();
      }

      display = drm_display::make_display(util::allocator::get_generic(), dri_device);
   });
   return display;
}

drm_display_mode::drm_display_mode()
   : m_drm_mode_info{}
   , m_preferred(false)
{
}

uint16_t drm_display_mode::get_width() const
{
   return m_drm_mode_info.hdisplay;
}

uint16_t drm_display_mode::get_height() const
{
   return m_drm_mode_info.vdisplay;
}

uint32_t drm_display_mode::get_refresh_rate() const
{
   /* DRM provides refresh rate in Hz and vulkan expects mHz */
   return m_drm_mode_info.vrefresh * 1000;
}

drmModeModeInfo drm_display_mode::get_drm_mode() const
{
   return m_drm_mode_info;
}

void drm_display_mode::set_drm_mode(drmModeModeInfo mode)
{
   m_drm_mode_info = mode;
}

bool drm_display_mode::is_preferred() const
{
   return m_preferred;
}

void drm_display_mode::set_preferred(bool preferred)
{
   m_preferred = preferred;
}

drm_display_mode *drm_display::get_display_modes_begin() const
{
   return m_display_modes.get();
}

drm_display_mode *drm_display::get_display_modes_end() const
{
   return m_display_modes.get() + m_num_display_modes;
}

size_t drm_display::get_num_display_modes() const
{
   return m_num_display_modes;
}

int drm_display::get_drm_fd() const
{
   return m_drm_fd.get();
}

uint32_t drm_display::get_connector_id() const
{
   return m_drm_connector->connector_id;
}

int drm_display::get_crtc_id() const
{
   return m_crtc_id;
}

drmModeConnector *drm_display::get_connector() const
{
   return m_drm_connector.get();
}

uint32_t drm_display::get_max_width() const
{
   return m_max_width;
}
uint32_t drm_display::get_max_height() const
{
   return m_max_height;
}

} /* namespace display */

} /* namespace wsi */
