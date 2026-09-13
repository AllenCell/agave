#pragma once

#include "gfxapi/IGraphicsDevice.h"

#include <unordered_map>

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

  gfxApi::ShaderHandle createShader(const gfxApi::ShaderDesc& desc) override;
  void destroyShader(gfxApi::ShaderHandle handle) override;

  gfxApi::ShaderProgramHandle createShaderProgram(const gfxApi::ShaderProgramDesc& desc) override;
  void destroyShaderProgram(gfxApi::ShaderProgramHandle handle) override;

  // Backend-internal lookup. Not part of the public gfxApi surface; renderer
  // code that still talks raw GL can use these escape hatches during the
  // migration. They will go away once all callers go through gfxApi.
  GLShader* lookupShader(gfxApi::ShaderHandle handle) const;
  GLShaderProgram* lookupProgram(gfxApi::ShaderProgramHandle handle) const;

private:
  uint64_t m_nextId = 1;
  std::unordered_map<uint64_t, GLShader*> m_shaders;
  std::unordered_map<uint64_t, GLShaderProgram*> m_programs;
};

} // namespace gfxopengl
