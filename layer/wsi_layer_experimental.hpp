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
 * @file wsi_layer_experimental.hpp
 *
 * @brief Contains the Vulkan definitions for experimental features.
 */
#pragma once

#include <vulkan/vulkan.h>
#include "util/macros.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
#define VK_KHR_present_timing 1
#define VK_KHR_PRESENT_TIMING_SPEC_VERSION 1
#define VK_KHR_PRESENT_TIMING_EXTENSION_NAME "VK_KHR_present_timing"

#define VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT ((VkResult)(-1000208000))
#define VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT ((VkTimeDomainEXT)(1000208000))
#define VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT ((VkTimeDomainEXT)(1000208001))
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT ((VkStructureType)1000208002)
#define VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT ((VkStructureType)1000208003)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT ((VkStructureType)1000208004)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT ((VkStructureType)1000208005)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT ((VkStructureType)1000208006)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT ((VkStructureType)1000208007)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT ((VkStructureType)1000208008)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT ((VkStructureType)1000208009)
#define VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT ((VkStructureType)1000208010)
#define VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT ((VkStructureType)1000208011)

typedef struct VkPhysicalDevicePresentTimingFeaturesEXT
{
   VkStructureType sType;
   void *pNext;
   VkBool32 presentTiming;
   VkBool32 presentAtAbsoluteTime;
   VkBool32 presentAtRelativeTime;
} VkPhysicalDevicePresentTimingFeaturesEXT;

typedef struct VkPresentTimingSurfaceCapabilitiesEXT
{
   VkStructureType sType;
   void *pNext;
   VkBool32 presentTimingSupported;
   VkBool32 presentAtAbsoluteTimeSupported;
   VkBool32 presentAtRelativeTimeSupported;
   VkPresentStageFlagsEXT presentStageQueries;
   VkPresentStageFlagsEXT presentStageTargets;
} VkPresentTimingSurfaceCapabilitiesEXT;

typedef enum VkPresentStageFlagBitsEXT
{
   VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT = 0x00000001,
   VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT = 0x00000002,
   VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT = 0x00000004,
   VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT = 0x00000008,
} VkPresentStageFlagBitsEXT;

typedef struct VkSwapchainTimingPropertiesEXT
{
   VkStructureType sType;
   const void *pNext;
   uint64_t refreshDuration;
   uint64_t variableRefreshDelay;
} VkSwapchainTimingPropertiesEXT;

typedef struct VkSwapchainTimeDomainPropertiesEXT
{
   VkStructureType sType;
   void *pNext;
   uint32_t timeDomainCount;
   VkTimeDomainEXT *pTimeDomains;
   uint64_t *pTimeDomainIds;
} VkSwapchainTimeDomainPropertiesEXT;

typedef struct VkSwapchainCalibratedTimestampInfoEXT
{
   VkStructureType sType;
   const void *pNext;
   VkSwapchainKHR swapchain;
   VkPresentStageFlagsEXT presentStage;
   uint64_t timeDomainId;
} VkSwapchainCalibratedTimestampInfoEXT;

typedef struct VkPresentStageTimeEXT
{
   VkPresentStageFlagsEXT stage;
   uint64_t time;
} VkPresentStageTimeEXT;

typedef struct VkPastPresentationTimingEXT
{
   VkStructureType sType;
   const void *pNext;
   uint64_t presentId;
   uint32_t presentStageCount;
   VkPresentStageTimeEXT *pPresentStages;
   VkTimeDomainEXT timeDomain;
   uint64_t timeDomainId;
   VkBool32 reportComplete;
} VkPastPresentationTimingEXT;

typedef struct VkPastPresentationTimingPropertiesEXT
{
   VkStructureType sType;
   const void *pNext;
   uint64_t timingPropertiesCounter;
   uint64_t timeDomainsCounter;
   uint32_t presentationTimingCount;
   VkPastPresentationTimingEXT *pPresentationTimings;
};

typedef struct VkPastPresentationTimingInfoEXT
{
   VkStructureType sType;
   const void *pNext;
   VkSwapchainKHR swapchain;
};

typedef union VkPresentTimeEXT
{
   uint64_t targetPresentTime;
   uint64_t presentDuration;
} VkPresentTimeEXT;

typedef struct VkPresentTimingInfoEXT
{
   VkStructureType sType;
   const void *pNext;
   VkPresentTimeEXT time;
   uint64_t timeDomainId;
   VkPresentStageFlagsEXT presentStageQueries;
   VkPresentStageFlagsEXT targetPresentStage;
   VkBool32 presentAtRelativeTime;
   VkBool32 presentAtNearestRefreshCycle;
} VkPresentTimingInfoEXT;

typedef struct VkPresentTimingsInfoEXT
{
   VkStructureType sType;
   const void *pNext;
   uint32_t swapchainCount;
   const VkPresentTimingInfoEXT *pTimingInfos;
} VkPresentTimingsInfoEXT;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkSetSwapchainPresentTimingQueueSizeEXT(VkDevice device, VkSwapchainKHR swapchain,
                                                  uint32_t size) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimingPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain,
                                            uint64_t *pSwapchainTimingPropertiesCounter,
                                            VkSwapchainTimingPropertiesEXT *pSwapchainTimingProperties) VWL_API_POST;
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimeDomainPropertiesEXT(
   VkDevice device, VkSwapchainKHR swapchain, uint64_t *pTimeDomainsCounter,
   VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPastPresentationTimingEXT(
   VkDevice device, const VkPastPresentationTimingInfoEXT *pPastPresentationTimingInfo,
   VkPastPresentationTimingPropertiesEXT *pPastPresentationTimingProperties) VWL_API_POST;
#endif
