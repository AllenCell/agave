#pragma once

#include <QSurfaceFormat>

class QOpenGLContext;

namespace gfxopengl {

// The QSurfaceFormat AGAVE renders with (GL version, depth/stencil sizes,
// core profile). Used both for windowed contexts and as the default format.
QSurfaceFormat getSurfaceFormat(bool enableDebug = false);

// Create a Qt OpenGL context configured with getSurfaceFormat(). Caller owns
// the returned context.
QOpenGLContext* createOpenGLContext();

} // namespace gfxopengl
