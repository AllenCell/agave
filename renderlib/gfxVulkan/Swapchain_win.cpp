#define VK_USE_PLATFORM_WIN32_KHR

#include "NativeSurface.h"
#include "Swapchain.h"

#if AGAVE_HAS_VULKAN && defined(_WIN32)

#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gfxvulkan {

bool
Swapchain::createNativeSurface()
{
  if (!m_backend) {
    return false;
  }

  m_vkSurface = createNativeWindowSurface(m_backend->instance(), m_surface);
  return m_vkSurface != VK_NULL_HANDLE;
}

VkSurfaceKHR
createNativeWindowSurface(VkInstance instance, gfxApi::IWindowSurface* surface)
{
  if (instance == VK_NULL_HANDLE || !surface) {
    return VK_NULL_HANDLE;
  }

  HWND hwnd = reinterpret_cast<HWND>(surface->nativeHandle());
  if (!hwnd) {
    LOG_ERROR << "Unable to get an HWND for the Vulkan window";
    return VK_NULL_HANDLE;
  }

  auto createWin32Surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
    vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
  if (!createWin32Surface) {
    LOG_ERROR << "vkCreateWin32SurfaceKHR is not available on the current Vulkan instance";
    return VK_NULL_HANDLE;
  }

  VkWin32SurfaceCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hinstance = GetModuleHandle(nullptr);
  createInfo.hwnd = hwnd;

  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  VkResult result = createWin32Surface(instance, &createInfo, nullptr, &vkSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateWin32SurfaceKHR failed with VkResult " << result;
    return VK_NULL_HANDLE;
  }

  return vkSurface;
}

void
Swapchain::updateNativeSurfaceLayout()
{
  // Nothing to do on Win32: the swapchain sizes itself from the HWND client
  // rect during ensureSwapchain(), and Windows lays the window out for us.
}

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN && defined(_WIN32)
