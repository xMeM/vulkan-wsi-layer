# WSI integration guide

This integration guide provides information relevant for a developer wishing to
extend the Vulkan® WSI Layer with support for a new windowing system. In Vulkan®
a windowing system is abstracted by a WSI platform and for each platform a specific
extension exists.

In the layer a Vulkan® WSI platform is represented by a WSI backend. Adding a
WSI backend means implementing a WSI extension. Two WSI extensions that are implemented
in the layer are `VK_KHR_wayland_surface` and `VK_EXT_headless_surface`.

## Directory Structure

Each WSI backend implementation resides in the `wsi` folder and has its own
directory. A new folder should be created for a new implementation,
for example `wsi/new_wsi`.

Each WSI backend implements the `surface`, `surface_properties`
and `swapchain` interfaces. A new WSI backend implementation should be structured
as follows, where a separate file is used to contain the implementation of each
interface:

```
wsi/new_wsi
├── surface.cpp
├── surface.hpp
├── surface_properties.cpp
├── surface_properties.hpp
├── swapchain.cpp
└── swapchain.hpp
```

## Build configuration

A new build option should be added to the [CMakeLists.txt](../CMakeLists.txt)
file to enable support for the new WSI backend. This allows compiling the layer
for platforms that do not have the necessary support for the backend.

Furthermore the extension that is implemented by the new backend must be added in
the `instance_extensions` list in the
[VkLayer_window_system_integration.json](../layer/VkLayer_window_system_integration.json)
file. For example, `VK_KHR_wayland_surface` is added to the JSON manifest when Wayland
support is enabled.

## Interfaces implementations

### surface_properties

This interface contains functions that will be called during the interception of
various Vulkan® surface related entrypoints. These functions should contain the
platform specific code required to support the new WSI backend. For example, some of
the functions that are intercepted are:
* vkGetPhysicalDeviceSurfaceCapabilitiesKHR
* vkGetPhysicalDeviceSurfaceCapabilitiesKHR2
* vkGetPhysicalDeviceSurfaceFormatsKHR
* vkGetPhysicalDeviceSurfaceFormatsKHR2
* vkGetPhysicalDeviceSurfacePresentModesKHR

A new WSI backend should implement the functions the interface consists of, which
are described in the [surface_properties.hpp](surface_properties.hpp) file.

Each WSI extension defines surface/platform specific entrypoints, which are only used by
each individual surface. These entrypoints are exposed to the application through
the `surface_properties::get_proc_addr` function, which returns a function pointer
to the implementation of the requested entrypoint. For example, if the extension for
a new WSI backend defines the entrypoint `vkCreateNewXyzSurface`, then `get_proc_addr`
should look like this.

```c++
VWL_VKAPI_CALL(VkResult) CreateNewXyzSurface(...) VWL_API_POST
{
   ...
}

PFN_vkVoidFunction surface_properties::get_proc_addr(const char *name)
{
   if (strcmp(name, "vkCreateNewXyzSurface") == 0)
   {
      return reinterpret_cast<PFN_vkVoidFunction>(CreateNewXyzSurface);
   }
   ...
}
```

> **_NOTE:_** The VWL_* macros are defined in [macros.hpp](../util/macros.hpp) and
are used to mark function signatures.

> **_NOTE:_** The extensions and their entrypoints are partially implemented by
the Vulkan® loader so an appropriate loader should be used.


If any of the extension's specific entrypoints needs to call the next implementation
in the chain (e.g. call the ICD's entrypoint implementation), depending on
whether it is an instance or device entrypoint either the
`INSTANCE_ENTRYPOINTS_LIST` or `DEVICE_ENTRYPOINTS_LIST` must be extended with
the name of the entrypoint. For example, for the `vkCreateNewXyzSurface` the instance
list should be extended.

```diff
diff --git a/layer/private_data.hpp b/layer/private_data.hpp
--- a/layer/private_data.hpp
+++ b/layer/private_data.hpp
@@ -73,6 +73,9 @@ namespace layer
    OPTIONAL(CreateHeadlessSurfaceEXT)                    \
    /* VK_KHR_wayland_surface */                          \
    OPTIONAL(CreateWaylandSurfaceKHR)                     \
+   /* VK_KHR_new_wsi */                                  \
+   OPTIONAL(CreateNewXyzSurface)                         \
    /* VK_KHR_get_surface_capabilities2 */                \
    OPTIONAL(GetPhysicalDeviceSurfaceCapabilities2KHR)    \
    OPTIONAL(GetPhysicalDeviceSurfaceFormats2KHR)         \
```

Furthermore, these lists must be extended when the implementation needs to make use of
entrypoints defined by other extensions. For example, if a WSI implementation
needs to import a fence payload from a POSIX file descriptor the `vkImportFenceFdKHR`
function can be used, which is provided by the `VK_KHR_external_fence_fd`
extension. In order for the entrypoint to become visible to the implementation,
`OPTIONAL(ImportFenceFdKHR)` must be added to the `DEVICE_ENTRYPOINTS_LIST`
(also an ICD that implements the extension must be used).

When a new entry is added to either of these lists the `disp` member variable of
the singleton `instance_private_data` or `device_private_data` object is extended with a
function pointer that has the same name as the one that was added in the list.
The entrypoint can be called by getting the `instance_private_data`/`device_private_data`
object and then using the function pointer in the `disp` attribute.
For example, a WSI implementation that calls down the chain for the
`vkCreateNewXyzSurface` function, should do the following.

```c++
auto &instance_data = layer::instance_private_data::get(instance);
VkResult res = instance_data.disp.CreateNewXyzSurface(...);
```

In order for the new `surface_properties` implementation to be picked up by the
common layer code the following changes must be applied to the
[wsi_factory.cpp](wsi_factory.cpp) file.
1. Conditionally include the header of the WSI specific `surface_properties` implementation.
1. Extend the `supported_wsi_extensions` with the new WSI.
1. Add a new case with the new WSI platform in `get_surface_properties`.

```diff
diff --git a/wsi/wsi_factory.cpp b/wsi/wsi_factory.cpp
--- a/wsi/wsi_factory.cpp
+++ b/wsi/wsi_factory.cpp
@@ -46,6 +46,10 @@
 #include "wayland/surface_properties.hpp"
 #endif

+#if BUILD_NEW_WSI
+#include "new_wsi/surface_properties.hpp"
+#endif /* BUILD_NEW_WSI */
+
 namespace wsi
 {

@@ -60,6 +64,9 @@ static struct wsi_extension
 #if BUILD_WSI_WAYLAND
    { { VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_WAYLAND },
 #endif
+#if BUILD_NEW_WSI
+   { { VK_KHR_NEW_WSI_EXTENSION_NAME, VK_KHR_NEW_WSI_SPEC_VERSION }, VK_ICD_WSI_PLATFORM_NEW_WSI },
+#endif /* BUILD_NEW_WSI */
 };

 static surface_properties *get_surface_properties(VkIcdWsiPlatform platform)
@@ -74,6 +81,10 @@ static surface_properties *get_surface_properties(VkIcdWsiPlatform platform)
    case VK_ICD_WSI_PLATFORM_WAYLAND:
       return &wayland::surface_properties::get_instance();
 #endif
+#if BUILD_NEW_WSI
+   case VK_ICD_WSI_PLATFORM_NEW_WSI:
+      return &new_wsi::surface_properties::get_instance();
+#endif /* BUILD_NEW_WSI */
    default:
       return nullptr;
    }

```

### surface

The surface interface represents a VkSurface. Each WSI backend's implementation
of this interface represents the platform specific surface. The `wsi::surface`
objects are associated with the corresponding `VkSurface` objects in
[`instance_private_data`](../layer/private_data.hpp) and they should be created
and linked to the `instance_data` during the specific `VkSurface` creation entrypoint.

A new WSI backend should implement the functions the interface consists of, which
are described in [surface.hpp](surface.hpp).

### swapchain

The common swapchain functionality is implemented in the `swapchain_base` class.
A new WSI backend should implement the virtual functions defined in
[swapchain_base.hpp](swapchain_base.hpp).

The base swapchain implementation has support for the FIFO presentation mode, which
makes use of the presentation thread. Also, it gives the option to disable the
thread in order to support other present modes like Mailbox. All of the WSI
implementations can use the FIFO mode without any modifications. If a WSI
implementation wants to leverage the Mailbox present mode extra synchronization
with the presentation engine may be needed.

The use of the presentation thread is enabled in the `init_platform` function
by setting the `use_presentation_thread` flag.

In the layer the swapchain images are represented by the `swapchain_image` struct.
This struct has a member variable which is called `data` and is of `void *` type.
This member variable is used to store the unique data that are needed by the images in
each WSI implementation.

Before submitting an image to the presentation engine the swapchain must wait
for the rendering operations on this image to finish. The synchronization primitives
used for this waiting operation by the WSI implementations are abstracted by the
classes defined in the [synchronization.hpp](synchronization.hpp) file. Currently only
Vulkan® fences and fences exportable to Sync FD are supported. The specific behaviour
that depends on the type of the synchronization primitive a backend uses is implemented
in the `image_set_present_payload` and `image_wait_present` functions.

## Helpers

Helper objects and utilities can be found in [util](../util). These helpers can
be used to aid development. For example, alternatives to the standard containers
(`std::vector`, `std::unordered_map`, ...) are provided to make it easier for the
Vulkan® WSI Layer code to conform to the allocation requirements of Vulkan®. Also,
logging macros have been defined there for logging messages with different priority.
A non exhaustive list follows with the useful helper objects and functions
implemented there:
* `util::vector`
* `util::allocator`
* `util::allocator::make_unique`
* `util::fd_owner`
* `util::ring_buffer`
* `util::unordered_map`
* `util::unordered_set`

And macros:
* `TRY`
* ` WSI_LOG_*`
