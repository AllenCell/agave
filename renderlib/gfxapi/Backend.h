#pragma once

#include "IGraphicsDevice.h"
#include "IGestureRenderer.h"
#include "IRenderWindow.h"
#include "Framebuffer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RenderSettings;

namespace gfxApi {

class IGLContext;

// Parameters supplied to a backend at construction time.
struct InitParams
{
  // Graphics backend requested by the application. OpenGL remains the default
  // so existing GUI/server startup paths are unchanged.
  BackendKind backendKind = BackendKind::OpenGL;
  // Filesystem path to renderer assets (shaders, etc.).
  std::string assetPath;
  // Run without an on-screen surface (offscreen / EGL rendering).
  bool headless = false;
  // Index of the GPU to use when more than one is available.
  int selectedGpu = 0;
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
// (currently always an OpenGL backend) and holds it for the process lifetime.
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
};

} // namespace gfxApi
