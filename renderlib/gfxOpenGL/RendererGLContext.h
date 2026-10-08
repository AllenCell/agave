#pragma once

#include "gfxapi/IGLContext.h"

#include <memory>

namespace gfxopengl {

class Backend;
class HeadlessGLContext;

// Wrap a GL context intended to run on a separate render thread. Backed by an
// EGL headless context when the backend is headless, otherwise by an
// application-supplied context.
class RendererGLContext : public gfxApi::IGLContext
{
public:
  explicit RendererGLContext(Backend& backend, gfxApi::IGLContext* externalContext = nullptr);
  ~RendererGLContext() override;

  // Install a pre-existing context. The application/windowing layer owns it.
  void configure(gfxApi::IGLContext* glContext = nullptr);

  // Create a headless EGL context or use the configured application context.
  void init();

  bool create() override;
  bool isValid() const override;
  bool makeCurrent() override;
  void doneCurrent() override;

  // Destroy any context owned here. Application-provided contexts are not owned.
  void destroy();

private:
  // Backend determines whether to create a headless EGL context or use the
  // application-supplied context.
  Backend& m_backend;

  std::unique_ptr<HeadlessGLContext> m_headlessContext;
  gfxApi::IGLContext* m_externalContext = nullptr;
  gfxApi::IGLContext* m_context = nullptr;
};

} // namespace gfxopengl
