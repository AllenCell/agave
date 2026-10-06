#include "QtGLContext.h"

#include "renderlib/Logging.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QThread>

namespace {

constexpr int kGLVersionMajor = 4;
constexpr int kGLVersionMinor = 1;
constexpr int kDepthBufferBits = 24;
constexpr int kStencilBufferBits = 8;

} // namespace

QtGLContext::QtGLContext(QOpenGLContext* context)
  : m_ownContext(context == nullptr)
  , m_context(context)
{
}

QtGLContext::~QtGLContext()
{
  destroy();
}

QSurfaceFormat
QtGLContext::defaultSurfaceFormat(bool enableDebug)
{
  QSurfaceFormat format;
  format.setDepthBufferSize(kDepthBufferBits);
  format.setStencilBufferSize(kStencilBufferBits);
  format.setVersion(kGLVersionMajor, kGLVersionMinor);
  format.setProfile(QSurfaceFormat::CoreProfile);
  if (enableDebug) {
    format.setOption(QSurfaceFormat::DebugContext);
  }
  return format;
}

void
QtGLContext::setDefaultSurfaceFormat(bool enableDebug)
{
  QSurfaceFormat::setDefaultFormat(defaultSurfaceFormat(enableDebug));
}

bool
QtGLContext::create()
{
  if (!m_context) {
    m_context = new QOpenGLContext();
    m_context->setFormat(defaultSurfaceFormat());
    if (!m_context->create()) {
      LOG_ERROR << "QtGLContext: failed to create OpenGL context";
      return false;
    }
  }

  if (!m_context->isValid()) {
    LOG_ERROR << "QtGLContext: OpenGL context is not valid";
    return false;
  }

  if (!m_surface) {
    m_surface = new QOffscreenSurface();
    m_surface->setFormat(m_context->format());
    m_surface->create();
  }

  if (!m_surface->isValid()) {
    LOG_ERROR << "QtGLContext: offscreen surface is not valid";
    return false;
  }

  return true;
}

bool
QtGLContext::isValid() const
{
  return m_context && m_context->isValid() && m_surface && m_surface->isValid();
}

bool
QtGLContext::makeCurrent()
{
  return isValid() && m_context->makeCurrent(m_surface);
}

void
QtGLContext::doneCurrent()
{
  if (m_context) {
    m_context->doneCurrent();
  }
}

void
QtGLContext::moveToThread(QThread* thread)
{
  if (m_context) {
    m_context->moveToThread(thread);
  }
  if (m_surface) {
    m_surface->moveToThread(thread);
  }
}

void
QtGLContext::destroy()
{
  doneCurrent();

  delete m_surface;
  m_surface = nullptr;

  if (m_ownContext) {
    delete m_context;
  }
  m_context = nullptr;
}
