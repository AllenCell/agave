#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XLIB_KHR

#include "NativeSurface.h"
#include "Swapchain.h"

#if AGAVE_HAS_VULKAN && !defined(__APPLE__) && !defined(_WIN32)

#include "Logging.h"

#include <X11/Xlib.h>
#include <xcb/xcb.h>

#include <mutex>

namespace gfxvulkan {

namespace {

// Fallback xcb connection used only when the IWindowSurface didn't hand us
// one from the windowing toolkit. Sharing a single process-wide connection is
// fine because it's only used for VkSurfaceKHR presentation; the OS reclaims
// it on exit.
xcb_connection_t*
fallbackXcbConnection()
{
  static std::once_flag once;
  static xcb_connection_t* connection = nullptr;
  std::call_once(once, []() {
    connection = xcb_connect(nullptr, nullptr);
    if (!connection || xcb_connection_has_error(connection)) {
      LOG_ERROR << "xcb_connect failed; unable to open an X server connection for Vulkan";
      if (connection) {
        xcb_disconnect(connection);
        connection = nullptr;
      }
    }
  });
  return connection;
}

// Fallback xlib display used only when the xcb surface extension isn't
// available and the IWindowSurface didn't hand us its own display. Held
// for the process lifetime for the same reason as the xcb fallback.
Display*
fallbackXlibDisplay()
{
  static std::once_flag once;
  static Display* display = nullptr;
  std::call_once(once, []() {
    display = XOpenDisplay(nullptr);
    if (!display) {
      LOG_ERROR << "XOpenDisplay failed; unable to open an X server connection for Vulkan";
    }
  });
  return display;
}

bool
tryCreateXcbSurface(VkInstance instance, gfxApi::IWindowSurface* surface, xcb_window_t window, VkSurfaceKHR& outSurface)
{
  auto createXcbSurface =
    reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR"));
  if (!createXcbSurface) {
    return false;
  }

  // Prefer the connection the windowing toolkit is already using so that
  // Vulkan presents on the same connection that owns the window. Some Mesa
  // drivers keep per-connection state for present timing / DRI3, and mixing
  // connections can cause subtle sync issues. Fall back to our own connection
  // only when the toolkit didn't provide one.
  auto* connection = reinterpret_cast<xcb_connection_t*>(surface->nativeDisplay());
  if (!connection) {
    connection = fallbackXcbConnection();
  }
  if (!connection) {
    return false;
  }

  VkXcbSurfaceCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  createInfo.connection = connection;
  createInfo.window = window;

  VkResult result = createXcbSurface(instance, &createInfo, nullptr, &outSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateXcbSurfaceKHR failed with VkResult " << result;
    outSurface = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool
tryCreateXlibSurface(VkInstance instance, Window window, VkSurfaceKHR& outSurface)
{
  auto createXlibSurface =
    reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
  if (!createXlibSurface) {
    return false;
  }

  Display* display = fallbackXlibDisplay();
  if (!display) {
    return false;
  }

  VkXlibSurfaceCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  createInfo.dpy = display;
  createInfo.window = window;

  VkResult result = createXlibSurface(instance, &createInfo, nullptr, &outSurface);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateXlibSurfaceKHR failed with VkResult " << result;
    outSurface = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

} // namespace

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

  // Qt's QWidget::winId() returns the X11 window handle; the same numeric
  // value is valid as both xcb_window_t and Xlib Window.
  const uintptr_t windowId = reinterpret_cast<uintptr_t>(surface->nativeHandle());
  if (windowId == 0) {
    LOG_ERROR << "Unable to get an X11 window ID for the Vulkan window";
    return VK_NULL_HANDLE;
  }

  // Prefer VK_KHR_xcb_surface (matches GLFW's preference: "VK_KHR_xcb_surface
  // is preferred due to some early ICDs exposing but not correctly
  // implementing VK_KHR_xlib_surface"). Fall back to VK_KHR_xlib_surface for
  // instances that only expose the xlib extension.
  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  if (tryCreateXcbSurface(instance, surface, static_cast<xcb_window_t>(windowId), vkSurface)) {
    return vkSurface;
  }
  if (tryCreateXlibSurface(instance, static_cast<Window>(windowId), vkSurface)) {
    return vkSurface;
  }

  LOG_ERROR << "Neither VK_KHR_xcb_surface nor VK_KHR_xlib_surface is available on the current Vulkan instance";
  return VK_NULL_HANDLE;
}

void
Swapchain::updateNativeSurfaceLayout()
{
  // Nothing to do on X11: the swapchain sizes itself from the window geometry
  // reported by vkGetPhysicalDeviceSurfaceCapabilitiesKHR during
  // ensureSwapchain(), and the X server / Qt lay the window out for us.
}

} // namespace gfxvulkan

#endif // AGAVE_HAS_VULKAN && !defined(__APPLE__) && !defined(_WIN32)
