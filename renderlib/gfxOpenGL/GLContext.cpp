#include "GLContext.h"

#include "Logging.h"

#include <QOpenGLContext>

namespace gfxopengl {

namespace {
// AGAVE's desired OpenGL configuration.
constexpr int kGLVersionMajor = 4;
constexpr int kGLVersionMinor = 1;
constexpr int kDepthBufferBits = 24;
constexpr int kStencilBufferBits = 8;
} // namespace

QSurfaceFormat
getSurfaceFormat(bool enableDebug)
{
  QSurfaceFormat format;
  format.setDepthBufferSize(kDepthBufferBits);
  format.setStencilBufferSize(kStencilBufferBits);
  format.setVersion(kGLVersionMajor, kGLVersionMinor);
  // necessary on MacOS at least:
  format.setProfile(QSurfaceFormat::CoreProfile);
  if (enableDebug) {
    format.setOption(QSurfaceFormat::DebugContext);
  }
  return format;
}

QOpenGLContext*
createOpenGLContext()
{
  QOpenGLContext* context = new QOpenGLContext();
  context->setFormat(getSurfaceFormat()); // ...and set the format on the context too

  bool createdOk = context->create();
  if (!createdOk) {
    LOG_ERROR << "Failed to create OpenGL Context";
  } else {
    LOG_INFO << "Created opengl context";
  }
  if (!context->isValid()) {
    LOG_ERROR << "Created GL Context is not valid";
  }

  return context;
}

} // namespace gfxopengl
