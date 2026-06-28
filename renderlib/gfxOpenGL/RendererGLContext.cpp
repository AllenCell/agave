#include "RendererGLContext.h"

#include "Backend.h"
#include "HeadlessGLContext.h"
#include "Logging.h"

namespace gfxopengl {

RendererGLContext::RendererGLContext(Backend& backend)
  : m_backend(backend)
{
}

RendererGLContext::~RendererGLContext() {}

void
RendererGLContext::destroy()
{
  m_context = nullptr;
  m_headlessContext.reset();
}

// to be run from main thread prior to starting render thread
void
RendererGLContext::configure(gfxApi::IGLContext* glContext)
{
  m_externalContext = glContext;
}

// to be run from render thread
// context is current when returning from this function.
// scenarios:
// headless linux (server mode): always use EGL
// gui / Qt paths: use the context supplied by agave_app
void
RendererGLContext::init()
{
  if (m_backend.headless()) {
    m_headlessContext = std::make_unique<HeadlessGLContext>(m_backend.eglDisplay());
    m_context = m_headlessContext.get();
  } else {
    m_context = m_externalContext;
  }

  if (!m_context || !m_context->isValid()) {
    LOG_ERROR << "RendererGLContext: no valid GL context available";
    return;
  }

  if (!m_context->makeCurrent()) {
    LOG_ERROR << "RendererGLContext: failed to make GL context current";
  }
}

void
RendererGLContext::makeCurrent()
{
  if (m_context && !m_context->makeCurrent()) {
    LOG_ERROR << "RendererGLContext: failed to make GL context current";
  }
}

void
RendererGLContext::doneCurrent()
{
  if (m_context) {
    m_context->doneCurrent();
  }
}

} // namespace gfxopengl
