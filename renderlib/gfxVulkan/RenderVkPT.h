#pragma once

#include "RenderVk.h"
#include "resources/SampledImage.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

namespace gfxvulkan {

class Framebuffer;

class RenderVkPT : public RenderVk
{
public:
  static const std::string TYPE_NAME;

  RenderVkPT(Backend& backend, RenderSettings* renderSettings);
  ~RenderVkPT() override;

  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void cleanUpResources() override;

protected:
  VolumeTextureMode volumeTextureMode() const override;
  bool usesProgressiveAccumulation() const override;
  float volumeShaderMode() const override;
  float rayStepCount() const override;

private:
  bool ensureFramebuffers(uint32_t w, uint32_t h);
  bool ensureFullscreenResources(VkFormat toneMapFormat);
  bool ensurePtVolumeResources();
  bool ensureDummyLutTexture();
  bool updateAccumDescriptorSet();
  bool updateToneMapDescriptorSet();
  bool updatePtVolumeDescriptorSet(VkImageView previousAccumView);
  bool updateAccumUniformBuffer();
  bool updateToneMapUniformBuffer(const CCamera& camera);
  bool updatePtVolumeUniforms(const CCamera& camera, int sampleCounter);
  void renderToFramebufferPT(const CCamera& camera, Framebuffer& framebuffer);
  void renderPtVolume(Framebuffer& target);
  void runAccumulationPass(Framebuffer& framebuffer);
  void runToneMapPass(Framebuffer& framebuffer);
  void transitionToShaderRead(Framebuffer& framebuffer);
  void destroyFullscreenResources();
  void destroyPipelines();
  std::unique_ptr<Framebuffer> m_displayFramebuffer;
  std::unique_ptr<Framebuffer> m_sampleFramebuffer;
  std::unique_ptr<Framebuffer> m_accumFramebuffer;
  std::unique_ptr<Framebuffer> m_accumScratchFramebuffer;

  resources::Buffer m_quadVertexBuffer;
  resources::Buffer m_quadIndexBuffer;
  uint32_t m_quadIndexCount = 0;

  resources::Buffer m_accumUniformBuffer;
  resources::Buffer m_toneMapUniformBuffer;
  resources::UniqueSampler m_framebufferSampler;

  resources::UniqueDescriptorSetLayout m_accumDescriptorSetLayout;
  resources::UniqueDescriptorPool m_accumDescriptorPool;
  VkDescriptorSet m_accumDescriptorSet = VK_NULL_HANDLE;
  resources::UniqueRenderPass m_accumRenderPass;
  resources::UniquePipelineLayout m_accumPipelineLayout;
  resources::UniquePipeline m_accumPipeline;

  resources::UniqueDescriptorSetLayout m_toneMapDescriptorSetLayout;
  resources::UniqueDescriptorPool m_toneMapDescriptorPool;
  VkDescriptorSet m_toneMapDescriptorSet = VK_NULL_HANDLE;
  resources::UniqueRenderPass m_toneMapRenderPass;
  resources::UniquePipelineLayout m_toneMapPipelineLayout;
  resources::UniquePipeline m_toneMapPipeline;
  VkFormat m_toneMapPipelineColorFormat = VK_FORMAT_UNDEFINED;

  // Per-sample Monte Carlo path-trace volume pass (pathTraceVolume.frag).
  resources::Buffer m_ptVolumeUniformBuffer;
  resources::UniqueDescriptorSetLayout m_ptVolumeDescriptorSetLayout;
  resources::UniqueDescriptorPool m_ptVolumeDescriptorPool;
  VkDescriptorSet m_ptVolumeDescriptorSet = VK_NULL_HANDLE;
  resources::UniqueRenderPass m_ptVolumeRenderPass;
  resources::UniquePipelineLayout m_ptVolumePipelineLayout;
  resources::UniquePipeline m_ptVolumePipeline;

  // 1x1 placeholder bound to the shader's deprecated g_lutTexture[4] sampler array.
  resources::SampledImage m_dummyLutTexture;
};

} // namespace gfxvulkan
