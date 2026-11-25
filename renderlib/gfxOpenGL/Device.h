#pragma once

#include "gfxapi/IGraphicsDevice.h"

class GLShader;
class GLShaderProgram;

namespace gfxopengl {

// OpenGL implementation of gfxApi::IGraphicsDevice. Owns the GL objects that
// back each issued handle.
//
// This implementation currently delegates to the existing GLShader /
// GLShaderProgram wrappers in this layer while the migration proceeds.
class Device : public gfxApi::IGraphicsDevice
{
public:
  Device();
  ~Device() override;

  gfxApi::BackendKind backend() const override { return gfxApi::BackendKind::OpenGL; }
};

} // namespace gfxopengl
