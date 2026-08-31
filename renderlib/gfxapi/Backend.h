#pragma once

#include "IGraphicsDevice.h"
#include "IGestureRenderer.h"
#include "IRenderWindow.h"
#include "Framebuffer.h"
#include "WindowSurface.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RenderSettings;

namespace gfxApi {

class IGLContext;

// InitParams::selectedGpu value meaning "pick the best compatible device".
constexpr int kAutoSelectGpu = -1;

// Parameters supplied to a backend at construction time.
struct InitParams
{
  // Graphics backend requested by the application.
  BackendKind backendKind =
#if AGAVE_HAS_VULKAN
    BackendKind::Vulkan;
#else
    BackendKind::OpenGL;
#endif
  // Filesystem path to renderer assets (shaders, etc.).
  std::string assetPath;
  // Run without an on-screen surface (offscreen / EGL rendering).
  bool headless = false;
  // Zero-based index of the GPU to use, in the backend's own enumeration order
  // (the order reported by --list_devices). kAutoSelectGpu means the backend
  // picks the best device that is compatible with the requested mode; an
  // explicit index disables that fallback, so an incompatible device is an
  // error rather than a silent switch to another one.
  int selectedGpu = kAutoSelectGpu;
  // Install a GL debug logger (verbose; for development).
  bool enableDebug = false;
  // Non-headless OpenGL context supplied by the application/windowing layer.
  // The backend does not own this context.
  IGLContext* windowedContext = nullptr;
  // Additional Vulkan instance extensions required by the windowing layer.
  // Vulkan backends always add their own required portability/debug extensions.
  std::vector<std::string> vulkanInstanceExtensions;
};

enum class RenderWindowKind : uint8_t
{
  PathTrace,
  RaymarchBlended,
};

// Abstract graphics backend. A backend owns the concrete IGraphicsDevice and
// any backend-global state. renderlib::initialize creates exactly one backend
// and holds it for the process lifetime.
// All renderer code should reach GPU functionality through device() rather
// than touching backend-specific APIs.
class Backend
{
public:
  virtual ~Backend() = default;

  // The GPU device owned by this backend.
  virtual IGraphicsDevice& device() = 0;

  // Renderer for gesture/manipulator UI draw commands.
  virtual std::unique_ptr<IGestureRenderer> createGestureRenderer() = 0;

  // GL context used by offscreen render threads. The returned object may wrap
  // an application-owned context, or own a backend-created headless context.
  virtual std::unique_ptr<IGLContext> createRendererContext(IGLContext* externalContext = nullptr) = 0;

  // Main volume renderer.
  virtual std::unique_ptr<IRenderWindow> createRenderWindow(RenderWindowKind kind, RenderSettings* renderSettings) = 0;

  // Backend-specific framebuffer implementation.
  virtual std::unique_ptr<Framebuffer> createFramebuffer(const FramebufferDesc& desc) = 0;

  // Clear the framebuffer currently bound by the platform/windowing layer.
  virtual void clearCurrentFramebuffer(const ClearColor& color) = 0;

  // Whether this backend was initialized for offscreen/headless rendering.
  virtual bool isHeadless() const = 0;

  // The kind of backend this is.
  virtual BackendKind kind() const = 0;

  // Two-part initialization.
  // We would like to initialize the renderlib graphics backend as early
  // as possible.  But some backends require the native window surface to
  // be available before they can select a suitable device that can
  // actually present to that surface. Call this once, after the
  // window exists and before creating any renderers.
  //
  // A null surface is only valid for a headless backend; in windowed mode it
  // is an error rather than an implicit switch to headless behavior. Backends
  // where the toolkit owns presentation (OpenGL/Qt) are fully initialized by
  // construction and keep this default implementation.
  virtual bool initDeviceForWindow(IWindowSurface* surface = nullptr)
  {
    (void)surface;
    return true;
  }
};

} // namespace gfxApi
