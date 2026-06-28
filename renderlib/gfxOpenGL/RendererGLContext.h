#pragma once

#include "gfxapi/IGLContext.h"

#include <memory>

namespace gfxopengl {

class Backend;
class HeadlessGLContext;

// Wrap a GL context intended to run on a separate thread (or be moved from the
// main thread and back). Backed by an EGL headless context when the backend is
// headless, otherwise by a Qt offscreen context.
class RendererGLContext
{
public:
  explicit RendererGLContext(Backend& backend);
  ~RendererGLContext();

  // Install a pre-existing context. The application/windowing layer owns it.
  void configure(gfxApi::IGLContext* glContext = nullptr);

  // Create a headless EGL context or use the configured application context.
  void init();

  // Destroy any context owned here. Application-provided contexts are not owned.
  void destroy();

  void makeCurrent();
  void doneCurrent();

private:
  // back end is used to determine whether to create a headless EGL context or a Qt offscreen context
  Backend& m_backend;

  std::unique_ptr<HeadlessGLContext> m_headlessContext;
  gfxApi::IGLContext* m_externalContext = nullptr;
  gfxApi::IGLContext* m_context = nullptr;
};

} // namespace gfxopengl
