#pragma once

#include "Descriptors.h"
#include "Handles.h"

namespace gfxApi {

// Identifies which backend implementation is in use. The renderlib bootstrap
// currently asserts that the active backend is OpenGL.
enum class BackendKind
{
  OpenGL,
  Vulkan,
  WebGPU,
};

// Backend-agnostic GPU device.
//
// This interface is intentionally minimal at this stage of the refactor; it
// will grow as more primitives are migrated behind the abstraction. The
// initial surface covers shader & shader-program creation, which is the most
// self-contained primitive in the existing renderer.
class IGraphicsDevice
{
public:
  virtual ~IGraphicsDevice() = default;

  virtual BackendKind backend() const = 0;

  // Shader objects -------------------------------------------------------
  virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
  virtual void destroyShader(ShaderHandle handle) = 0;

  // Shader programs ------------------------------------------------------
  virtual ShaderProgramHandle createShaderProgram(const ShaderProgramDesc& desc) = 0;
  virtual void destroyShaderProgram(ShaderProgramHandle handle) = 0;
};

} // namespace gfxApi
