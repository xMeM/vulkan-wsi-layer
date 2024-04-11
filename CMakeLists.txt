# Copyright (c) 2019-2024 Arm Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

cmake_minimum_required(VERSION 3.4.3)
project(VkLayer_window_system_integration)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VULKAN_PKG_CONFIG vulkan)

find_program(CLANG_TIDY clang-tidy-8)

if (NOT CLANG_TIDY STREQUAL "CLANG_TIDY-NOTFOUND")
   message(STATUS "Using clang-tidy: ${CLANG_TIDY}")
   set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY} -checks=bugprone-*,modernize-*)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pthread -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

if(NOT DEFINED VULKAN_CXX_INCLUDE)
   set(VULKAN_CXX_INCLUDE ${VULKAN_PKG_CONFIG_INCLUDEDIR})
endif()

if(DEFINED VULKAN_CXX_INCLUDE)
   message(STATUS "Using Vulkan include directories: ${VULKAN_CXX_INCLUDE}")
   separate_arguments(VULKAN_CXX_INCLUDE)
else()
   message(FATAL_ERROR "Either vulkan.pc must be available or VULKAN_CXX_INCLUDE must be defined")
endif()

# Build Configuration options
option(BUILD_WSI_HEADLESS "Build with support for VK_EXT_headless_surface" ON)
option(BUILD_WSI_WAYLAND "Build with support for VK_KHR_wayland_surface" OFF)
option(BUILD_WSI_DISPLAY "Build with support for VK_KHR_display" OFF)
option(BUILD_WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN "Build with support for VK_EXT_image_compression_control_swapchain" OFF)
option(BUILD_WSI_DISPLAY_SUPPORT_FORMAT_MODIFIERS "Build with support for format modifiers in VK_KHR_display" ON)
set(SELECT_EXTERNAL_ALLOCATOR "none" CACHE STRING "Select an external system allocator (none, ion)")
set(EXTERNAL_WSIALLOC_LIBRARY "" CACHE STRING "External implementation of the wsialloc interface to use")

if(BUILD_WSI_WAYLAND OR BUILD_WSI_DISPLAY)
   set(BUILD_DRM_UTILS true)
   if(SELECT_EXTERNAL_ALLOCATOR STREQUAL "none")
      message(FATAL_ERROR "WSI only supported with an external allocator.")
   endif()
endif()

if(BUILD_DRM_UTILS)
   add_library(drm_utils STATIC util/drm/drm_utils.cpp)

   pkg_check_modules(LIBDRM REQUIRED libdrm)
   message(STATUS "Using libdrm include directories: ${LIBDRM_INCLUDE_DIRS}")
   message(STATUS "Using libdrm cflags: ${LIBDRM_CFLAGS}")

   target_sources(drm_utils PRIVATE util/drm/format_table.c)
   target_include_directories(drm_utils PRIVATE ${VULKAN_CXX_INCLUDE})
   target_include_directories(drm_utils PUBLIC ${LIBDRM_INCLUDE_DIRS})
   target_include_directories(drm_utils PUBLIC util/wsialloc)
   target_compile_options(drm_utils PUBLIC ${LIBDRM_CFLAGS})
endif()

# External WSI Allocator
if(NOT SELECT_EXTERNAL_ALLOCATOR STREQUAL "none" AND EXTERNAL_WSIALLOC_LIBRARY STREQUAL "")
   add_library(wsialloc STATIC)
   set_target_properties(wsialloc PROPERTIES C_STANDARD 99)

   if(SELECT_EXTERNAL_ALLOCATOR STREQUAL "ion")
      target_sources(wsialloc PRIVATE util/wsialloc/wsialloc_ion.c)
      target_link_libraries(wsialloc drm_utils)
      if(DEFINED KERNEL_DIR)
         target_include_directories(wsialloc PRIVATE "${KERNEL_DIR}/drivers/staging/android/uapi")
      else()
         message(FATAL_ERROR "KERNEL_DIR must be defined as the root of the Linux kernel source.")
      endif()
   else()
      message(FATAL_ERROR "Invalid external allocator selected: ${SELECT_EXTERNAL_ALLOCATOR}")
   endif()

   target_include_directories(wsialloc PRIVATE ${VULKAN_CXX_INCLUDE})
   target_include_directories(wsialloc PRIVATE util/drm)
endif()

# Wayland WSI
if(BUILD_WSI_WAYLAND)
   add_library(wayland_wsi STATIC
      wsi/wayland/surface_properties.cpp
      wsi/wayland/surface.cpp
      wsi/wayland/wl_helpers.cpp
      wsi/wayland/swapchain.cpp)

   pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
   message(STATUS "Using Wayland client include directories: ${WAYLAND_CLIENT_INCLUDE_DIRS}")
   message(STATUS "Using Wayland client cflags: ${WAYLAND_CLIENT_CFLAGS}")
   message(STATUS "Using Wayland client ldflags: ${WAYLAND_CLIENT_LDFLAGS}")

   find_program(WAYLAND_SCANNER_EXEC wayland-scanner REQUIRED)
   message(STATUS "Using wayland-scanner : ${WAYLAND_SCANNER_EXEC}")

   pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
   pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
   message(STATUS "Using wayland protocols dir : ${WAYLAND_PROTOCOLS_DIR}")

   add_custom_target(wayland_generated_files
      COMMAND ${WAYLAND_SCANNER_EXEC} client-header
      ${WAYLAND_PROTOCOLS_DIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
      ${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-unstable-v1-client-protocol.h
      COMMAND ${WAYLAND_SCANNER_EXEC} public-code
      ${WAYLAND_PROTOCOLS_DIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
      ${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-unstable-v1-protocol.c
      COMMAND ${WAYLAND_SCANNER_EXEC} client-header
      ${WAYLAND_PROTOCOLS_DIR}/unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml
      ${CMAKE_CURRENT_BINARY_DIR}/linux-explicit-synchronization-unstable-v1-protocol.h
      COMMAND ${WAYLAND_SCANNER_EXEC} public-code
      ${WAYLAND_PROTOCOLS_DIR}/unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml
      ${CMAKE_CURRENT_BINARY_DIR}/linux-explicit-synchronization-unstable-v1-protocol.c
      BYPRODUCTS linux-dmabuf-unstable-v1-protocol.c linux-dmabuf-unstable-v1-client-protocol.h
                 linux-explicit-synchronization-unstable-v1-protocol.c linux-explicit-synchronization-unstable-v1-protocol.h)

   target_sources(wayland_wsi PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-unstable-v1-protocol.c
      ${CMAKE_CURRENT_BINARY_DIR}/linux-dmabuf-unstable-v1-client-protocol.h
      ${CMAKE_CURRENT_BINARY_DIR}/linux-explicit-synchronization-unstable-v1-protocol.c
      ${CMAKE_CURRENT_BINARY_DIR}/linux-explicit-synchronization-unstable-v1-protocol.h)
   add_dependencies(wayland_wsi wayland_generated_files)

   target_include_directories(wayland_wsi PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${VULKAN_CXX_INCLUDE}
      ${WAYLAND_CLIENT_INCLUDE_DIRS}
      ${CMAKE_CURRENT_BINARY_DIR})

   target_compile_options(wayland_wsi PRIVATE ${WAYLAND_CLIENT_CFLAGS})
   target_compile_options(wayland_wsi INTERFACE "-DBUILD_WSI_WAYLAND=1")
   if(NOT EXTERNAL_WSIALLOC_LIBRARY STREQUAL "")
      target_link_libraries(wayland_wsi ${EXTERNAL_WSIALLOC_LIBRARY})
   else()
      target_link_libraries(wayland_wsi wsialloc)
   endif()
   target_link_libraries(wayland_wsi drm_utils ${WAYLAND_CLIENT_LDFLAGS})
   list(APPEND LINK_WSI_LIBS wayland_wsi)
else()
   list(APPEND JSON_COMMANDS COMMAND sed -i '/VK_KHR_wayland_surface/d' ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_window_system_integration.json)
endif()

# Headless
if(BUILD_WSI_HEADLESS)
   add_library(wsi_headless STATIC
      wsi/headless/surface_properties.cpp
      wsi/headless/surface.cpp
      wsi/headless/swapchain.cpp)

   target_include_directories(wsi_headless PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${VULKAN_CXX_INCLUDE}
      ${CMAKE_CURRENT_BINARY_DIR})

   target_compile_options(wsi_headless INTERFACE "-DBUILD_WSI_HEADLESS=1")
   list(APPEND LINK_WSI_LIBS wsi_headless)
else()
   list(APPEND JSON_COMMANDS COMMAND sed -i '/VK_EXT_headless_surface/d' ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_window_system_integration.json)
   list(APPEND JSON_COMMANDS COMMAND sed -i '/VK_KHR_shared_presentable_image/d' ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_window_system_integration.json)
endif()

# Display
if (BUILD_WSI_DISPLAY)
   add_library(wsi_display STATIC
      wsi/display/drm_display.cpp
      wsi/display/surface_properties.cpp
      wsi/display/swapchain.cpp
      wsi/display/surface.cpp)

   pkg_check_modules(LIBDRM REQUIRED libdrm)
   message(STATUS "Using libdrm include directories: ${LIBDRM_INCLUDE_DIRS}")
   message(STATUS "Using libdrm ldflags: ${LIBDRM_LDFLAGS}")

   target_include_directories(wsi_display PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${VULKAN_CXX_INCLUDE}
      ${CMAKE_CURRENT_BINARY_DIR})

   target_include_directories(wsi_display PUBLIC
      ${LIBDRM_INCLUDE_DIRS})

   target_compile_options(wsi_display INTERFACE "-DBUILD_WSI_DISPLAY=1")
   target_link_libraries(wsi_display ${LIBDRM_LDFLAGS} drm)
   target_link_libraries(wsi_display drm_utils)
   if(NOT EXTERNAL_WSIALLOC_LIBRARY STREQUAL "")
      target_link_libraries(wsi_display ${EXTERNAL_WSIALLOC_LIBRARY})
   else()
      target_link_libraries(wsi_display wsialloc)
   endif()
   list(APPEND LINK_WSI_LIBS wsi_display)
else()
   list(APPEND JSON_COMMANDS COMMAND sed -i '/VK_KHR_display/d' ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_window_system_integration.json)
endif()

# Layer
add_library(${PROJECT_NAME} SHARED
   layer/layer.cpp
   layer/private_data.cpp
   layer/surface_api.cpp
   layer/swapchain_api.cpp
   util/timed_semaphore.cpp
   util/custom_allocator.cpp
   util/extension_list.cpp
   util/log.cpp
   util/format_modifiers.cpp
   wsi/external_memory.cpp
   wsi/surface_properties.cpp
   wsi/swapchain_base.cpp
   wsi/synchronization.cpp
   wsi/wsi_factory.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE ${WSI_DEFINES})
target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${VULKAN_CXX_INCLUDE})

if (BUILD_WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN)
   add_definitions("-DWSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN=1")
else()
   add_definitions("-DWSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN=0")
   list(APPEND JSON_COMMANDS COMMAND sed -i '/VK_EXT_image_compression_control_swapchain/d' ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_window_system_integration.json)
endif()

if (BUILD_WSI_DISPLAY_SUPPORT_FORMAT_MODIFIERS)
   add_definitions("-DWSI_DISPLAY_SUPPORT_FORMAT_MODIFIERS=1")
else()
   add_definitions("-DWSI_DISPLAY_SUPPORT_FORMAT_MODIFIERS=0")
endif()

target_link_libraries(${PROJECT_NAME} ${LINK_WSI_LIBS})

add_custom_target(manifest_json ALL COMMAND
   cp ${PROJECT_SOURCE_DIR}/layer/VkLayer_window_system_integration.json ${CMAKE_CURRENT_BINARY_DIR}
   ${JSON_COMMANDS})
