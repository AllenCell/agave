#pragma once

#include "RenderVk.h"

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
  VkShaderModule createShaderModule(const uint32_t* words, size_t wordCount) const;

  std::unique_ptr<Framebuffer> m_displayFramebuffer;
  std::unique_ptr<Framebuffer> m_sampleFramebuffer;
  std::unique_ptr<Framebuffer> m_accumFramebuffer;
  std::unique_ptr<Framebuffer> m_accumScratchFramebuffer;

  VkBuffer m_quadVertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_quadVertexMemory = VK_NULL_HANDLE;
  VkBuffer m_quadIndexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_quadIndexMemory = VK_NULL_HANDLE;
  uint32_t m_quadIndexCount = 0;

  VkBuffer m_accumUniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_accumUniformMemory = VK_NULL_HANDLE;
  VkBuffer m_toneMapUniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_toneMapUniformMemory = VK_NULL_HANDLE;
  VkSampler m_framebufferSampler = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_accumDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_accumDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_accumDescriptorSet = VK_NULL_HANDLE;
  VkRenderPass m_accumRenderPass = VK_NULL_HANDLE;
  VkPipelineLayout m_accumPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_accumPipeline = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_toneMapDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_toneMapDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_toneMapDescriptorSet = VK_NULL_HANDLE;
  VkRenderPass m_toneMapRenderPass = VK_NULL_HANDLE;
  VkPipelineLayout m_toneMapPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_toneMapPipeline = VK_NULL_HANDLE;
  VkFormat m_toneMapPipelineColorFormat = VK_FORMAT_UNDEFINED;

  // Per-sample Monte Carlo path-trace volume pass (pathTraceVolume.frag).
  VkBuffer m_ptVolumeUniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_ptVolumeUniformMemory = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_ptVolumeDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_ptVolumeDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_ptVolumeDescriptorSet = VK_NULL_HANDLE;
  VkRenderPass m_ptVolumeRenderPass = VK_NULL_HANDLE;
  VkPipelineLayout m_ptVolumePipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_ptVolumePipeline = VK_NULL_HANDLE;

  // 1x1 placeholder bound to the shader's deprecated g_lutTexture[4] sampler array.
  VkImage m_dummyLutImage = VK_NULL_HANDLE;
  VkDeviceMemory m_dummyLutMemory = VK_NULL_HANDLE;
  VkImageView m_dummyLutView = VK_NULL_HANDLE;
  VkSampler m_dummyLutSampler = VK_NULL_HANDLE;
};

} // namespace gfxvulkan
