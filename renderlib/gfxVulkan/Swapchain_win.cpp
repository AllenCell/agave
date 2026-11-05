#define VK_USE_PLATFORM_WIN32_KHR

#include "Swapchain.h"

#if AGAVE_HAS_VULKAN && defined(_WIN32)

#include "Logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gfxvulkan {

bool
Swapchain::createNativeSurface()
{
  if (!m_backend || !m_surface) {
    return false;
  }

  HWND hwnd = reinterpret_cast<HWND>(m_surface->nativeHandle());
  if (!hwnd) {
    LOG_ERROR << "Unable to get an HWND for the Vulkan window";
    return false;
  }

  auto createWin32Surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
    vkGetInstanceProcAddr(m_backend->instance(), "vkCreateWin32SurfaceKHR"));
  if (!createWin32Surface) {
    LOG_ERROR << "vkCreateWin32SurfaceKHR is not available on the current Vulkan instance";
    return false;
  }

  VkWin32SurfaceCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hinstance = GetModuleHandle(nullptr);
  createInfo.hwnd = hwnd;

  VkResult result = createWin32Surface(m_backend->instance(), &createInfo, nullptr, &m_vkSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateWin32SurfaceKHR failed with VkResult " << result;
    m_vkSurface = VK_NULL_HANDLE;
    return false;
  }

  return true;
}

void
Swapchain::updateNativeSurfaceLayout()
{
  // Nothing to do on Win32: the swapchain sizes itself from the HWND client
  // rect during ensureSwapchain(), and Windows lays the window out for us.
}

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN && defined(_WIN32)
