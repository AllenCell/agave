#include "RendererGLContext.h"

#include "Backend.h"
#include "HeadlessGLContext.h"
#include "Logging.h"

namespace gfxopengl {

RendererGLContext::RendererGLContext(Backend& backend, gfxApi::IGLContext* externalContext)
  : m_backend(backend)
  , m_externalContext(externalContext)
{
}

RendererGLContext::~RendererGLContext()
{
  destroy();
}

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

bool
RendererGLContext::create()
{
  destroy();

  if (m_backend.headless()) {
    m_headlessContext = std::make_unique<HeadlessGLContext>(m_backend.eglDisplay());
    m_context = m_headlessContext.get();
  } else {
    m_context = m_externalContext;
  }

  if (!m_context || !m_context->isValid()) {
    LOG_ERROR << "RendererGLContext: no valid GL context available";
    return false;
  }

  if (!m_context->makeCurrent()) {
    LOG_ERROR << "RendererGLContext: failed to make GL context current";
    return false;
  }

  if (!gladLoadGL()) {
    LOG_ERROR << "RendererGLContext: failed to load GL (gladLoadGL)";
    return false;
  }

  return true;
}

void
RendererGLContext::init()
{
  create();
}

bool
RendererGLContext::isValid() const
{
  return m_context && m_context->isValid();
}

bool
RendererGLContext::makeCurrent()
{
  if (!m_context) {
    LOG_ERROR << "RendererGLContext: no GL context available";
    return false;
  }

  if (!m_context->makeCurrent()) {
    LOG_ERROR << "RendererGLContext: failed to make GL context current";
    return false;
  }

  return true;
}

void
RendererGLContext::doneCurrent()
{
  if (m_context) {
    m_context->doneCurrent();
  }
}

} // namespace gfxopengl
