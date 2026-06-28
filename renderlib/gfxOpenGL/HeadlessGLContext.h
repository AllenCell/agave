#pragma once

#include "gfxapi/IGLContext.h"

namespace gfxopengl {

// A windowless OpenGL context created via EGL (used for headless / offscreen
// rendering on Linux). On platforms without EGL support (Windows, macOS) the
// methods are no-ops and the object carries no state.
//
// The context is created on a caller-supplied EGLDisplay; this class does not
// own the display and will not initialize or terminate it. The display is
// passed as void* so this header does not need to pull in <EGL/egl.h>.
class HeadlessGLContext : public gfxApi::IGLContext
{
public:
  // eglDisplay: an already-initialized EGLDisplay (an EGLDisplay value passed
  // as void*). Must outlive this context.
  explicit HeadlessGLContext(void* eglDisplay);
  ~HeadlessGLContext();

  // True if the underlying EGL context was created successfully.
  bool isValid() const override { return m_eglContext != nullptr; }

  bool create() override { return isValid(); }
  bool makeCurrent() override;
  void doneCurrent() override;

private:
  void* m_eglDisplay; // EGLDisplay, not owned
  void* m_eglContext; // EGLContext created on m_eglDisplay
};

} // namespace gfxopengl
