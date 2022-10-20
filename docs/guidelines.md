# Guidelines for development
This document outlines the guidelines that should be followed when submitting changes to the WSI layer. Although these
rules should not be taken as set in stone, we encourage contributors to follow them in order to make the WSI layer
more maintainable and easier to review.

## Exceptions
We discourage the use of exceptions in the codebase. The preferred error handling mechanism is to return
error codes. This closely aligns to the way the Vulkan API returns VkResult error codes to
indicate an error or success. In addition, some of the libraries used in the layer are written using C rather
than C++. In order to ensure interoperability between C and C++ code in the layer we have decided to forgo exceptions
as there is no way for the C code to clean up correctly when an exception is thrown in C++ code.

In some cases you might find that avoiding exceptions is very difficult. As an example we can use the STL
containers such as std::vector or std::map which, by design, throw exceptions for memory allocation failures.
In addition, the Vulkan API allows the application to supply custom allocation callbacks for memory allocation by
passing a VkAllocationCallbacks structure. We must be able to handle these exceptions, possibly caused by failures in
custom allocators, STL containers, etc., in order to correctly implement the Vulkan API and return the appropriate
error codes, e.g. VK_ERROR_OUT_OF_HOST_MEMORY.

A good idea is to introduce a wrapper around the STL container to ensure all exceptions are caught and converted to
error codes that are propagated back to the caller. It is highly recommended to use the
[utility classes](https://gitlab.freedesktop.org/mesa/vulkan-wsi-layer/-/tree/master/util) provided by the WSI layer
if you need an exception-safe wrapper for STL containers:
 * [util::vector](https://gitlab.freedesktop.org/mesa/vulkan-wsi-layer/-/blob/master/util/custom_allocator.hpp)
 * [util::unordered_map](https://gitlab.freedesktop.org/mesa/vulkan-wsi-layer/-/blob/master/util/unordered_map.hpp)
 * [util::unordered_set](https://gitlab.freedesktop.org/mesa/vulkan-wsi-layer/-/blob/master/util/unordered_set.hpp)

For other helper components provided by the WSI layer please see
[WSI integration document](https://gitlab.freedesktop.org/mesa/vulkan-wsi-layer/-/blob/master/wsi/README.md#helpers).