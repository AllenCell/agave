#pragma once

#include "RenderVk.h"
#include "resources/SampledImage.h"

#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

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
  bool ensureAccumVkFramebuffers();
  // (Re)create the VkFramebuffer for this frame's tone-map target. Not cached
  // across frames: the caller's framebuffer may be destroyed and recreated
  // between calls, and a recycled view handle would alias a stale object.
  bool ensureToneMapVkFramebuffer(Framebuffer& framebuffer);
  bool ensureFullscreenResources(VkFormat toneMapFormat);
  bool ensurePtVolumeResources();
  bool ensureDummyLutTexture();
  bool updateAccumDescriptorSet();
  bool updateToneMapDescriptorSet();
  // Write both ping-pong descriptor sets: set k samples accumulation image k
  // as tPreviousTexture. The uniform binding is dynamic so one set serves
  // every iteration of a batch.
  bool updatePtVolumeDescriptorSets();
  bool updateAccumUniformBuffer();
  bool updateToneMapUniformBuffer(const CCamera& camera);
  // Fill `iterations` consecutive slots of the per-iteration uniform ring,
  // starting at sample index `firstSample`.
  bool updatePtVolumeUniforms(const CCamera& camera, int firstSample, uint32_t iterations);
  // Grow the per-iteration uniform ring to hold at least `count` slots.
  bool ensurePtUniformCapacity(uint32_t count);
  // Rebuild and upload the majorant grid if anything it depends on changed,
  // and fill the grid fields of the uniform block (`uniforms` points at the
  // file-local PtVolumeUniforms being assembled).
  bool updateMajorantGrid(void* uniforms, float maxOpacity);
  void renderToFramebufferPT(const CCamera& camera, Framebuffer& framebuffer);
  // Record one path-trace iteration: reads m_accum[previousIndex] and writes
  // m_accum[targetIndex] using uniform slot `dynamicOffset`.
  void recordPtVolumePass(VkCommandBuffer commandBuffer, int targetIndex, int previousIndex, uint32_t dynamicOffset);
  void runAccumulationPass(Framebuffer& framebuffer);
  void recordToneMapPass(VkCommandBuffer commandBuffer, Framebuffer& framebuffer);
  void destroyFullscreenResources();
  void destroyPipelines();
  std::unique_ptr<Framebuffer> m_displayFramebuffer;
  std::unique_ptr<Framebuffer> m_sampleFramebuffer;
  // Ping-pong accumulation targets. m_accum[m_accumIndex] holds the current
  // running estimate; the other one is the scratch target for the next sample.
  std::array<std::unique_ptr<Framebuffer>, 2> m_accum;
  int m_accumIndex = 0;
  // VkFramebuffer objects wrapping the accumulation color views, created once
  // per resize rather than once per draw.
  std::array<resources::UniqueFramebuffer, 2> m_accumVkFramebuffers;
  // VkFramebuffer for the current frame's tone-map target. Kept as a member so
  // it outlives the submission that uses it and is released on the next frame.
  resources::UniqueFramebuffer m_toneMapVkFramebuffer;

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
  // The uniform buffer is a ring of `m_ptUniformCapacity` slots, each
  // `m_ptUniformStride` bytes, so a whole batch of iterations can be recorded
  // into one command buffer with a dynamic offset per draw.
  resources::Buffer m_ptVolumeUniformBuffer;
  void* m_ptUniformMapped = nullptr;
  VkDeviceSize m_ptUniformStride = 0;
  uint32_t m_ptUniformCapacity = 0;
  resources::UniqueDescriptorSetLayout m_ptVolumeDescriptorSetLayout;
  resources::UniqueDescriptorPool m_ptVolumeDescriptorPool;
  std::array<VkDescriptorSet, 2> m_ptVolumeDescriptorSets = { VK_NULL_HANDLE, VK_NULL_HANDLE };
  resources::UniqueRenderPass m_ptVolumeRenderPass;
  resources::UniquePipelineLayout m_ptVolumePipelineLayout;
  resources::UniquePipeline m_ptVolumePipeline;

  // 1x1 placeholder bound to the shader's deprecated g_lutTexture[4] sampler array.
  resources::SampledImage m_dummyLutTexture;

  // Inputs the current majorant grid was built from; see updateMajorantGrid.
  std::vector<float> m_majorantKey;
};

} // namespace gfxvulkan
