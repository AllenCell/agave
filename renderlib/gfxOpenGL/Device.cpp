#include "Device.h"

#include "Backend.h"
#include "Logging.h"
#include "Util.h"

namespace gfxopengl {

namespace {

GLenum
toGlShaderType(gfxApi::ShaderStage stage)
{
  switch (stage) {
    case gfxApi::ShaderStage::Vertex:
      return GL_VERTEX_SHADER;
    case gfxApi::ShaderStage::Fragment:
      return GL_FRAGMENT_SHADER;
    case gfxApi::ShaderStage::Geometry:
      return GL_GEOMETRY_SHADER;
    case gfxApi::ShaderStage::Compute:
      return GL_COMPUTE_SHADER;
  }
  return GL_VERTEX_SHADER;
}

} // namespace

Device::Device() = default;

Device::~Device()
{
  for (auto& kv : m_programs) {
    delete kv.second;
  }
  for (auto& kv : m_shaders) {
    delete kv.second;
  }
}

gfxApi::ShaderHandle
Device::createShader(const gfxApi::ShaderDesc& desc)
{
  if (desc.sourceKind != gfxApi::ShaderSourceKind::GLSL) {
    LOG_ERROR << "gfxopengl::Device only accepts GLSL shader sources";
    return {};
  }

  auto* shader = new GLShader(toGlShaderType(desc.stage));
  if (!shader->compileSourceCode(desc.source.c_str())) {
    LOG_ERROR << "Shader compile failed (" << desc.debugName << "): " << shader->log();
    delete shader;
    return {};
  }

  const uint64_t id = m_nextId++;
  m_shaders.emplace(id, shader);
  return gfxApi::ShaderHandle{ id };
}

void
Device::destroyShader(gfxApi::ShaderHandle handle)
{
  auto it = m_shaders.find(handle.id);
  if (it == m_shaders.end()) {
    return;
  }
  delete it->second;
  m_shaders.erase(it);
}

gfxApi::ShaderProgramHandle
Device::createShaderProgram(const gfxApi::ShaderProgramDesc& desc)
{
  auto* program = new GLShaderProgram();
  for (auto shaderHandle : desc.shaders) {
    GLShader* shader = lookupShader(shaderHandle);
    if (shader == nullptr) {
      LOG_ERROR << "gfxopengl::Device::createShaderProgram: invalid shader handle";
      delete program;
      return {};
    }
    program->addShader(shader);
  }
  if (!program->link()) {
    LOG_ERROR << "Shader program link failed (" << desc.debugName << "): " << program->log();
    delete program;
    return {};
  }

  const uint64_t id = m_nextId++;
  m_programs.emplace(id, program);
  return gfxApi::ShaderProgramHandle{ id };
}

void
Device::destroyShaderProgram(gfxApi::ShaderProgramHandle handle)
{
  auto it = m_programs.find(handle.id);
  if (it == m_programs.end()) {
    return;
  }
  delete it->second;
  m_programs.erase(it);
}

GLShader*
Device::lookupShader(gfxApi::ShaderHandle handle) const
{
  auto it = m_shaders.find(handle.id);
  return it == m_shaders.end() ? nullptr : it->second;
}

GLShaderProgram*
Device::lookupProgram(gfxApi::ShaderProgramHandle handle) const
{
  auto it = m_programs.find(handle.id);
  return it == m_programs.end() ? nullptr : it->second;
}

} // namespace gfxopengl
