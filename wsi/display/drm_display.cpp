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
#include "wsi/surface.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <mutex>
#include <drm_fourcc.h>
namespace wsi
{

namespace display
{

const std::string default_dri_device_name{ "/dev/dri/card0" };

drm_display::drm_display(util::fd_owner drm_fd, int crtc_id, drm_connector_owner drm_connector,
                         util::unique_ptr<util::vector<drm_format_pair>> supported_formats,
                         util::unique_ptr<drm_display_mode> display_modes, size_t num_display_modes, uint32_t max_width,
                         uint32_t max_height, bool supports_fb_modifiers)
   : m_drm_fd(std::move(drm_fd))
   , m_crtc_id(crtc_id)
   , m_drm_connector(std::move(drm_connector))
   , m_supported_formats(std::move(supported_formats))
   , m_display_modes(std::move(display_modes))
   , m_num_display_modes(num_display_modes)
   , m_max_width(max_width)
   , m_max_height(max_height)
   , m_supports_fb_modifiers(supports_fb_modifiers)
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

static bool find_primary_plane(const util::fd_owner &drm_fd, const drm_plane_resources_owner &plane_res,
                               drm_plane_owner &primary_plane, uint32_t &primary_plane_index)
{
   for (uint32_t i = 0; i < plane_res->count_planes; i++)
   {
      drm_plane_owner temp_plane{ drmModeGetPlane(drm_fd.get(), plane_res->planes[i]) };
      if (temp_plane != nullptr)
      {
         drm_object_properties_owner props{ drmModeObjectGetProperties(drm_fd.get(), plane_res->planes[i],
                                                                       DRM_MODE_OBJECT_PLANE) };
         if (props == nullptr)
         {
            continue;
         }

         for (uint32_t j = 0; j < props->count_props; j++)
         {
            drm_property_owner prop{ drmModeGetProperty(drm_fd.get(), props->props[j]) };
            if (prop == nullptr)
            {
               continue;
            }

            if (!strcmp(prop->name, "type"))
            {
               if (props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)
               {
                  primary_plane = std::move(temp_plane);
                  primary_plane_index = i;
                  return true;
               }
            }
         }
      }
   }
   return false;
}

static bool fill_supported_formats(const drm_plane_owner &primary_plane,
                                   util::vector<drm_format_pair> &supported_formats)
{
   for (uint32_t i = 0; i < primary_plane->count_formats; i++)
   {
      if (!supported_formats.try_push_back(drm_format_pair{ primary_plane->formats[i], DRM_FORMAT_MOD_LINEAR }))
      {
         WSI_LOG_ERROR("Out of host memory.");
         return false;
      }
   }

   return true;
}

static bool fill_supported_formats_with_modifiers(uint32_t primary_plane_index, const util::fd_owner &drm_fd,
                                                  const drm_plane_resources_owner &plane_res,
                                                  util::vector<drm_format_pair> &supported_formats)
{
   drm_object_properties_owner object_properties{ drmModeObjectGetProperties(
      drm_fd.get(), plane_res->planes[primary_plane_index], DRM_MODE_OBJECT_PLANE) };
   if (object_properties == nullptr)
   {
      return false;
   }

   for (uint32_t i = 0; i < object_properties->count_props; i++)
   {
      drm_property_owner property{ drmModeGetProperty(drm_fd.get(), object_properties->props[i]) };
      if (property == nullptr)
      {
         continue;
      }

      if (!strcmp(property->name, "IN_FORMATS"))
      {
         drmModeFormatModifierIterator iter{};
         drm_property_blob_owner blob{ drmModeGetPropertyBlob(drm_fd.get(), object_properties->prop_values[i]) };
         if (blob == nullptr)
         {
            return false;
         }

         while (drmModeFormatModifierBlobIterNext(blob.get(), &iter))
         {
            if (!supported_formats.try_push_back(drm_format_pair{ iter.fmt, iter.mod }))
            {
               return false;
            }
         }
      }
   }

   return true;
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

   /* Allow userspace to query native primary plane information */
   if (drmSetClientCap(drm_fd.get(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
   {
      return std::nullopt;
   }

   drm_plane_resources_owner plane_res{ drmModeGetPlaneResources(drm_fd.get()) };
   if (plane_res == nullptr || plane_res->count_planes == 0)
   {
      return std::nullopt;
   }

   uint32_t primary_plane_index = std::numeric_limits<uint32_t>::max();
   drm_plane_owner primary_plane{ nullptr };

   if (!find_primary_plane(drm_fd, plane_res, primary_plane, primary_plane_index))
   {
      WSI_LOG_ERROR("Failed to find primary plane for display.");
      return std::nullopt;
   }

   assert(primary_plane != nullptr);
   assert(primary_plane_index != std::numeric_limits<uint32_t>::max());

   bool supports_fb_modifiers = false;

#if WSI_DISPLAY_SUPPORT_FORMAT_MODIFIERS
   uint64_t addfb2_modifier_support = 0;
   if (drmGetCap(drm_fd.get(), DRM_CAP_ADDFB2_MODIFIERS, &addfb2_modifier_support) == 0)
   {
      supports_fb_modifiers = addfb2_modifier_support;
   }
#endif

   auto supported_formats = allocator.make_unique<util::vector<drm_format_pair>>(allocator);

   if (supports_fb_modifiers)
   {
      if (!fill_supported_formats_with_modifiers(primary_plane_index, drm_fd, plane_res, *supported_formats))
      {
         /* Fall back to the linear formats */
         if (!fill_supported_formats(primary_plane, *supported_formats))
         {
            return std::nullopt;
         }
      }
   }
   else
   {
      if (!fill_supported_formats(primary_plane, *supported_formats))
      {
         return std::nullopt;
      }
   }

   std::copy(display_modes.begin(), display_modes.end(), display_modes_mem.get());

   drm_display display{
      std::move(drm_fd),    crtc_id,   std::move(connector), std::move(supported_formats), std::move(display_modes_mem),
      display_modes.size(), max_width, max_height,           supports_fb_modifiers
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

const util::vector<drm_format_pair> *drm_display::get_supported_formats() const
{
   return m_supported_formats.get();
}

bool drm_display::is_format_supported(const drm_format_pair &format) const
{
   auto supported_format =
      std::find_if(m_supported_formats->begin(), m_supported_formats->end(), [format](const auto &supported_format) {
         return format.fourcc == supported_format.fourcc && format.modifier == supported_format.modifier;
      });

   return supported_format != m_supported_formats->end();
}

bool drm_display::supports_fb_modifiers() const
{
   return m_supports_fb_modifiers;
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
