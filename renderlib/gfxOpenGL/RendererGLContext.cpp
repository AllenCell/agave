#include "RendererGLContext.h"

#include "Backend.h"
#include "GLContext.h"
#include "HeadlessGLContext.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

namespace gfxopengl {

RendererGLContext::RendererGLContext(Backend& backend)
  : m_backend(backend)
{
}

RendererGLContext::~RendererGLContext() {}

void
RendererGLContext::destroy()
{
  if (m_ownGLContext) {
    delete m_glContext;
    delete m_eglContext;
  } else {
    if (m_glContext)
      m_glContext->moveToThread(QGuiApplication::instance()->thread());
  }

  // schedule this to be deleted only after we're done cleaning up
  if (m_surface)
    m_surface->deleteLater();
}

// to be run from main thread prior to starting render thread
void
RendererGLContext::configure(QOpenGLContext* glContext)
{
  // TODO what do we do when running on Linux desktop??
  // need a "don't bother with EGL switch"?
  if (m_backend.headless()) {
  } else {
    if (glContext) {
      m_glContext = glContext;
      m_ownGLContext = false;
    }
  }
}

void
RendererGLContext::initQOpenGLContext()
{
  if (m_ownGLContext) {
    this->m_glContext = createOpenGLContext();
  }

  this->m_surface = new QOffscreenSurface();
  this->m_surface->setFormat(this->m_glContext->format());
  this->m_surface->create();

  this->m_glContext->makeCurrent(m_surface);
}

// to be run from render thread
// context is current when returning from this function.
// scenarios:
// headless linux (server mode): always use EGL
// gui linux: always use QOpenGLContext
// else: use QOpenGLContext
void
RendererGLContext::init()
{
  if (m_backend.headless()) {
    this->m_eglContext = new HeadlessGLContext(m_backend.eglDisplay());
    this->m_eglContext->makeCurrent();
  } else {
    initQOpenGLContext();
  }
}

void
RendererGLContext::makeCurrent()
{
  // exactly one of the two contexts is created (see init())
  if (m_eglContext) {
    this->m_eglContext->makeCurrent();
  } else {
    this->m_glContext->makeCurrent(this->m_surface);
  }
}

void
RendererGLContext::doneCurrent()
{
  if (m_glContext)
    m_glContext->doneCurrent();
  if (m_eglContext)
    this->m_eglContext->doneCurrent();
}

} // namespace gfxopengl
