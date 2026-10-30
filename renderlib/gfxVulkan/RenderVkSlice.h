#pragma once

#include "RenderVk.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <memory>

namespace gfxvulkan {

class Framebuffer;

class RenderVkSlice : public RenderVk
{
public:
  RenderVkSlice(Backend& backend, RenderSettings* renderSettings);
  ~RenderVkSlice() override;

  void initialize(uint32_t w, uint32_t h) override;
  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void cleanUpResources() override;

protected:
  VolumeTextureMode volumeTextureMode() const override;

private:
  void renderToFramebufferSlice(const CCamera& camera, Framebuffer& framebuffer);
  bool ensureResources(VkFormat colorFormat);
  bool updateUniformBuffer(const CCamera& camera, uint32_t viewportWidth, uint32_t viewportHeight);
  bool updateDescriptorSet();
  void destroyPipeline();
  void destroySliceResources();
  gfxApi::ClearColor backgroundClearColor() const;

  std::unique_ptr<Framebuffer> m_internalFramebuffer;
  resources::Buffer m_sliceUniformBuffer;
  resources::UniqueDescriptorSetLayout m_sliceDescriptorSetLayout;
  resources::UniqueDescriptorPool m_sliceDescriptorPool;
  VkDescriptorSet m_sliceDescriptorSet = VK_NULL_HANDLE;
  resources::UniqueRenderPass m_sliceRenderPass;
  resources::UniquePipelineLayout m_slicePipelineLayout;
  resources::UniquePipeline m_slicePipeline;
  VkFormat m_slicePipelineColorFormat = VK_FORMAT_UNDEFINED;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_sliceStartTime;
};

} // namespace gfxvulkan
