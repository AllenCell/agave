#pragma once

#if AGAVE_HAS_VULKAN

#include "gfxapi/WindowSurface.h"

#include <vulkan/vulkan.h>

namespace gfxvulkan {

// Create a backend-owned VkSurfaceKHR from application-supplied native window
// details. The caller owns and must destroy the returned surface with
// vkDestroySurfaceKHR(instance, surface, nullptr).
VkSurfaceKHR
createNativeWindowSurface(VkInstance instance, gfxApi::IWindowSurface* surface);

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN
