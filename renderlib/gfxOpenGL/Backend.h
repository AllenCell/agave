#pragma once

#include "glad/include/glad/glad.h"

#include "Device.h"
#include "gfxapi/Backend.h"

#include <memory>

class RenderSettings;

namespace gfxopengl {

class HeadlessGLContext;

// OpenGL implementation of gfxApi::Backend. Owns the OpenGL graphics device and,
// in headless mode, performs the process-wide one-time EGL initialization and
// owns a current headless GL context for the process lifetime.
//
// Windowed (non-headless) GL context creation is handled by the application;
// this backend only manages the EGL / headless path.
class Backend : public gfxApi::Backend
{
public:
  // Makes a bootstrap GL context current (application-supplied or EGL) and
  // loads the GL entry points.
  explicit Backend(const gfxApi::InitParams& params);
  ~Backend() override;

  gfxApi::IGraphicsDevice& device() override { return m_device; }
  std::unique_ptr<gfxApi::IGestureRenderer> createGestureRenderer() override;
  std::unique_ptr<gfxApi::IGLContext> createRendererContext(gfxApi::IGLContext* externalContext = nullptr) override;
  std::unique_ptr<gfxApi::IRenderWindow> createRenderWindow(gfxApi::RenderWindowKind kind,
                                                            RenderSettings* renderSettings) override;
  std::unique_ptr<gfxApi::Framebuffer> createFramebuffer(const gfxApi::FramebufferDesc& desc) override;
  void clearCurrentFramebuffer(const gfxApi::ClearColor& color) override;
  bool isHeadless() const override { return headless(); }
  gfxApi::BackendKind kind() const override { return gfxApi::BackendKind::OpenGL; }

  // Whether this backend was created for headless (offscreen / EGL) rendering.
  bool headless() const { return m_params.headless; }

  // True if the backend constructed successfully: a bootstrap GL context was
  // created and made current, and the GL entry points loaded. A failed backend
  // should be discarded (see createGraphicsBackend in renderlib).
  bool isValid() const { return m_valid; }

  // The EGLDisplay created during headless initialization, returned as an
  // EGLDisplay value (void*). Null when not headless or EGL is unavailable.
  // Callers that create their own per-thread HeadlessGLContext need this.
  void* eglDisplay() const { return m_eglDisplay; }

  // Enumerate and log the available EGL devices. Services the --list-devices
  // option without fully constructing/initializing a backend.
  static void listDevices(int selectedGpu);

  // True if this build supports headless (EGL) rendering. Headless requests are
  // only honored on platforms where this returns true. constexpr so callers can
  // select code paths at compile time via `if constexpr`.
  static constexpr bool supportsHeadless()
  {
#if defined(__APPLE__) || defined(_WIN32)
    return false;
#else
    return true;
#endif
  }

private:
  // Bootstrap steps, each called once from the constructor. Return false (and
  // log) on failure.
  bool initEGLContext();          // headless EGL display + context (EGL builds only)
  bool initWindowedContext();     // application-provided GL context
  bool initGL();                  // load GL entry points (glad) + optional debug logging

  gfxApi::InitParams m_params;
  Device m_device;

  void* m_eglDisplay = nullptr;                         // EGLDisplay, owned (eglTerminate on destroy)
  std::unique_ptr<HeadlessGLContext> m_headlessContext; // bootstrap context when headless
  bool m_valid = false;
};

} // namespace gfxopengl
