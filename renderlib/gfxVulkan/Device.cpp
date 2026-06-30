#include "Device.h"

#include "Logging.h"

#include <shaderc/shaderc.hpp>

#include <cstring>

namespace gfxvulkan {

namespace {

shaderc_shader_kind
toShadercKind(gfxApi::ShaderStage stage)
{
  switch (stage) {
    case gfxApi::ShaderStage::Vertex:
      return shaderc_vertex_shader;
    case gfxApi::ShaderStage::Fragment:
      return shaderc_fragment_shader;
    case gfxApi::ShaderStage::Geometry:
      return shaderc_geometry_shader;
    case gfxApi::ShaderStage::Compute:
      return shaderc_compute_shader;
  }
  return shaderc_vertex_shader;
}

std::vector<uint32_t>
compileGlslToSpirv(const gfxApi::ShaderDesc& desc)
{
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetSourceLanguage(shaderc_source_language_glsl);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
  options.SetOptimizationLevel(shaderc_optimization_level_performance);

  const char* name = desc.debugName.empty() ? "agave-vulkan-shader" : desc.debugName.c_str();
  shaderc::SpvCompilationResult result =
    compiler.CompileGlslToSpv(desc.source, toShadercKind(desc.stage), name, options);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    LOG_ERROR << "Vulkan GLSL compile failed (" << name << "): " << result.GetErrorMessage();
    return {};
  }

  return { result.cbegin(), result.cend() };
}

std::vector<uint32_t>
spirvFromString(const std::string& source, const std::string& debugName)
{
  if (source.size() % sizeof(uint32_t) != 0) {
    LOG_ERROR << "SPIR-V shader payload size is not 32-bit aligned (" << debugName << ")";
    return {};
  }

  std::vector<uint32_t> spirv(source.size() / sizeof(uint32_t));
  if (!spirv.empty()) {
    std::memcpy(spirv.data(), source.data(), source.size());
  }
  return spirv;
}

} // namespace

Device::Device() = default;

Device::~Device()
{
  release();
}

void
Device::initialize(VkDevice device)
{
  m_device = device;
}

void
Device::release()
{
  if (m_device == VK_NULL_HANDLE) {
    m_shaders.clear();
    m_programs.clear();
    return;
  }

  for (const auto& kv : m_shaders) {
    if (kv.second.module != VK_NULL_HANDLE) {
      vkDestroyShaderModule(m_device, kv.second.module, nullptr);
    }
  }
  m_shaders.clear();
  m_programs.clear();
  m_device = VK_NULL_HANDLE;
}

gfxApi::ShaderHandle
Device::createShader(const gfxApi::ShaderDesc& desc)
{
  if (m_device == VK_NULL_HANDLE) {
    LOG_ERROR << "gfxvulkan::Device::createShader called before logical device initialization";
    return {};
  }

  std::vector<uint32_t> spirv;
  switch (desc.sourceKind) {
    case gfxApi::ShaderSourceKind::GLSL:
      spirv = compileGlslToSpirv(desc);
      break;
    case gfxApi::ShaderSourceKind::SPIRV:
      spirv = spirvFromString(desc.source, desc.debugName);
      break;
    case gfxApi::ShaderSourceKind::WGSL:
    default:
      LOG_ERROR << "gfxvulkan::Device does not accept WGSL shader sources";
      return {};
  }

  if (spirv.empty()) {
    return {};
  }

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = spirv.size() * sizeof(uint32_t);
  createInfo.pCode = spirv.data();

  VkShaderModule shaderModule = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateShaderModule failed for " << desc.debugName << " with VkResult " << result;
    return {};
  }

  const uint64_t id = m_nextId++;
  m_shaders.emplace(id, ShaderRecord{ shaderModule, desc.stage });
  return gfxApi::ShaderHandle{ id };
}

void
Device::destroyShader(gfxApi::ShaderHandle handle)
{
  auto it = m_shaders.find(handle.id);
  if (it == m_shaders.end()) {
    return;
  }

  if (m_device != VK_NULL_HANDLE && it->second.module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, it->second.module, nullptr);
  }
  m_shaders.erase(it);
}

gfxApi::ShaderProgramHandle
Device::createShaderProgram(const gfxApi::ShaderProgramDesc& desc)
{
  for (auto shaderHandle : desc.shaders) {
    if (m_shaders.find(shaderHandle.id) == m_shaders.end()) {
      LOG_ERROR << "gfxvulkan::Device::createShaderProgram: invalid shader handle";
      return {};
    }
  }

  const uint64_t id = m_nextId++;
  m_programs.emplace(id, ShaderProgramRecord{ desc.shaders });
  return gfxApi::ShaderProgramHandle{ id };
}

void
Device::destroyShaderProgram(gfxApi::ShaderProgramHandle handle)
{
  m_programs.erase(handle.id);
}

VkShaderModule
Device::shaderModule(gfxApi::ShaderHandle handle) const
{
  auto it = m_shaders.find(handle.id);
  return it == m_shaders.end() ? VK_NULL_HANDLE : it->second.module;
}

gfxApi::ShaderStage
Device::shaderStage(gfxApi::ShaderHandle handle) const
{
  auto it = m_shaders.find(handle.id);
  return it == m_shaders.end() ? gfxApi::ShaderStage::Vertex : it->second.stage;
}

} // namespace gfxvulkan
