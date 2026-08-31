#pragma once

#include <cstdint>

namespace gfxApi {

// Platform-native window surface information supplied by the application layer.
// Backends that need explicit presentation setup (Vulkan) use this to create a
// backend-specific surface; backends where the toolkit owns presentation
// (OpenGL/QOpenGLWidget) can ignore it.
//
// TODO: the nativeHandle()/nativeDisplay() pair is enough for XCB-style X11 and
// for Windows/macOS, but a backend currently has to guess which windowing
// system it is looking at. Wayland in particular wants to be told "this is a
// wl_surface/wl_display" rather than inferred, so this interface should grow a
// platform/kind field.
class IWindowSurface
{
public:
  virtual ~IWindowSurface() = default;

  // Platform-native window handle.
  //   macOS:   NSView*
  //   Windows: HWND
  //   X11:     xcb_window_t (as a pointer-sized value)
  virtual void* nativeHandle() const = 0;

  // Optional platform-native display / connection handle.
  //   X11:     xcb_connection_t*
  //   Wayland: wl_display*
  virtual void* nativeDisplay() const { return nullptr; }

  // True when the surface is visible and can be rendered to.
  virtual bool isExposed() const = 0;

  // Size of the surface in physical pixels.
  virtual void pixelSize(uint32_t& width, uint32_t& height) const = 0;

  // Ratio of physical pixels to logical points.
  virtual double contentScale() const = 0;
};

} // namespace gfxApi
