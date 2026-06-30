#pragma once

#include "gfxapi/IGraphicsDevice.h"

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

namespace gfxvulkan {

class Device : public gfxApi::IGraphicsDevice
{
public:
  Device();
  ~Device() override;

  void initialize(VkDevice device);
  void release();

  gfxApi::BackendKind backend() const override { return gfxApi::BackendKind::Vulkan; }

  gfxApi::ShaderHandle createShader(const gfxApi::ShaderDesc& desc) override;
  void destroyShader(gfxApi::ShaderHandle handle) override;

  gfxApi::ShaderProgramHandle createShaderProgram(const gfxApi::ShaderProgramDesc& desc) override;
  void destroyShaderProgram(gfxApi::ShaderProgramHandle handle) override;

  VkShaderModule shaderModule(gfxApi::ShaderHandle handle) const;
  gfxApi::ShaderStage shaderStage(gfxApi::ShaderHandle handle) const;

private:
  struct ShaderRecord
  {
    VkShaderModule module = VK_NULL_HANDLE;
    gfxApi::ShaderStage stage = gfxApi::ShaderStage::Vertex;
  };

  struct ShaderProgramRecord
  {
    std::vector<gfxApi::ShaderHandle> shaders;
  };

  uint64_t m_nextId = 1;
  VkDevice m_device = VK_NULL_HANDLE;
  std::unordered_map<uint64_t, ShaderRecord> m_shaders;
  std::unordered_map<uint64_t, ShaderProgramRecord> m_programs;
};

} // namespace gfxvulkan
