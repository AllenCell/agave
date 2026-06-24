#pragma once

class QOpenGLContext;
class QOffscreenSurface;

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

  void configure(QOpenGLContext* glContext = nullptr);
  void init();
  void destroy();

  void makeCurrent();
  void doneCurrent();

private:
  Backend& m_backend;
  bool m_ownGLContext = true;
  // only one of the following two can be non-null
  HeadlessGLContext* m_eglContext = nullptr;
  QOpenGLContext* m_glContext = nullptr;
  QOffscreenSurface* m_surface = nullptr;

  void initQOpenGLContext();
};

} // namespace gfxopengl
