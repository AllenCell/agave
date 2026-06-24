#include "HeadlessGLContext.h"

#include "Logging.h"

// EGL is only available on Linux in this project.
#if defined(__APPLE__) || defined(_WIN32)
#define GFXOPENGL_HAS_EGL 0
#else
#define GFXOPENGL_HAS_EGL 1
#endif

#if GFXOPENGL_HAS_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace gfxopengl {

#if GFXOPENGL_HAS_EGL
namespace {
// GL context configuration for headless rendering. Kept in sync with the
// surface format renderlib requests for the windowed path.
constexpr int kGLVersionMajor = 4;
constexpr int kGLVersionMinor = 1;
constexpr int kDepthBufferBits = 24;
constexpr int kStencilBufferBits = 8;
} // namespace
#endif

HeadlessGLContext::HeadlessGLContext(void* eglDisplay)
  : m_eglDisplay(eglDisplay)
  , m_eglContext(nullptr)
{
#if GFXOPENGL_HAS_EGL
  EGLDisplay dpy = static_cast<EGLDisplay>(m_eglDisplay);
  EGLint lastError = EGL_SUCCESS;

  // Bind the API
  EGLBoolean bindapi_ok = eglBindAPI(EGL_OPENGL_API);
  if (bindapi_ok == EGL_FALSE) {
    LOG_ERROR << "HeadlessGLContext, eglBindAPI failed";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  // Select an appropriate configuration
  EGLint numConfigs;
  EGLConfig eglCfg;

  static const EGLint configAttribs[] = { EGL_SURFACE_TYPE,
                                          EGL_PBUFFER_BIT,
                                          EGL_ALPHA_SIZE,
                                          8,
                                          EGL_BLUE_SIZE,
                                          8,
                                          EGL_GREEN_SIZE,
                                          8,
                                          EGL_RED_SIZE,
                                          8,
                                          EGL_DEPTH_SIZE,
                                          kDepthBufferBits,
                                          EGL_STENCIL_SIZE,
                                          kStencilBufferBits,
                                          EGL_RENDERABLE_TYPE,
                                          EGL_OPENGL_BIT,
                                          EGL_NONE };
  EGLBoolean chooseConfig_ok = eglChooseConfig(dpy, configAttribs, &eglCfg, 1, &numConfigs);
  if (chooseConfig_ok == EGL_FALSE) {
    LOG_ERROR << "HeadlessGLContext, eglChooseConfig failed";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  // Create a context and make it current
  static const EGLint contextAttribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, kGLVersionMajor, EGL_CONTEXT_MINOR_VERSION, kGLVersionMinor, EGL_NONE
  };
  EGLContext eglCtx = eglCreateContext(dpy, eglCfg, EGL_NO_CONTEXT, contextAttribs);
  if (eglCtx == EGL_NO_CONTEXT) {
    LOG_ERROR << "HeadlessGLContext, eglCreateContext failed";
  } else {
    LOG_INFO << "created a egl context";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  m_eglContext = eglCtx;
#endif
}

HeadlessGLContext::~HeadlessGLContext()
{
#if GFXOPENGL_HAS_EGL
  eglDestroyContext(static_cast<EGLDisplay>(m_eglDisplay), static_cast<EGLContext>(m_eglContext));
#endif
}

void
HeadlessGLContext::makeCurrent()
{
#if GFXOPENGL_HAS_EGL
  eglMakeCurrent(
    static_cast<EGLDisplay>(m_eglDisplay), EGL_NO_SURFACE, EGL_NO_SURFACE, static_cast<EGLContext>(m_eglContext));
#endif
}

void
HeadlessGLContext::doneCurrent()
{
#if GFXOPENGL_HAS_EGL
  eglMakeCurrent(static_cast<EGLDisplay>(m_eglDisplay), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif
}

} // namespace gfxopengl
