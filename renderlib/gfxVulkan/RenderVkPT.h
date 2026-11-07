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

// Vulkan progressive path tracer.
//
// How a frame is produced, in plain terms:
//
// 1. The path-trace fragment shader (pathTraceVolume.frag) computes ONE new
//    Monte Carlo sample per pixel and averages it into the running estimate.
//    It reads the estimate from one "accumulation" image and writes the new
//    estimate into a second one. Each iteration swaps the roles of the two
//    images ("ping-pong"), so no copy is needed to carry the result forward.
// 2. After the requested number of iterations, a tone-map pass converts the
//    high-dynamic-range estimate into the 8-bit image the caller asked for.
//
// Vulkan vocabulary used throughout this class:
// - A *command buffer* is a list of GPU commands recorded on the CPU and sent
//   to the GPU in one go ("submitted"). Submitting and then waiting for the
//   GPU to finish is expensive, so we record everything a frame needs (all
//   iterations plus the tone map) into a single command buffer and submit
//   exactly once per frame.
// - A *descriptor set* tells a shader which buffers and textures its inputs
//   refer to. Writing one is cheap but must happen before the command buffer
//   that uses it is submitted.
// - A *uniform buffer* holds the shader's constant inputs (camera, lights,
//   transfer function, ...). A *dynamic* uniform descriptor lets us bind one
//   large buffer once and pick a different slice of it per draw call with a
//   byte offset, which is how every iteration gets its own sample counter.
// - A *render pass* plus a *VkFramebuffer* describe which image a draw call
//   renders into. VkFramebuffer objects are tied to specific image views, so
//   we keep them alive as long as the images they wrap.
// - An *image layout transition* ("pipeline barrier") tells the GPU that an
//   image is about to be used differently (written as a render target, then
//   read as a texture). It also makes the GPU wait for the earlier use to
//   finish, which is how consecutive iterations are kept in order.
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
  // Create (or recreate after a resize) the two accumulation images.
  bool ensureFramebuffers(uint32_t w, uint32_t h);
  // Create the VkFramebuffer wrappers for the two accumulation images. Done
  // once per resize instead of once per draw, since the images do not change.
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
  // Render one frame (all iterations plus the tone map) into `framebuffer`.
  // This is the function that builds and submits the single command buffer.
  void renderToFramebufferPT(const CCamera& camera, Framebuffer& framebuffer);
  // Record (not run) one path-trace iteration into an already-open command
  // buffer: it reads m_accum[previousIndex], writes m_accum[targetIndex], and
  // takes its uniforms from the ring slot that starts `dynamicOffset` bytes
  // into m_ptVolumeUniformBuffer. Nothing executes until the buffer is
  // submitted by the caller.
  void recordPtVolumePass(VkCommandBuffer commandBuffer, int targetIndex, int previousIndex, uint32_t dynamicOffset);
  void runAccumulationPass(Framebuffer& framebuffer);
  // Record the tone-map draw into the same command buffer, reading the final
  // accumulation image and writing the caller's framebuffer.
  void recordToneMapPass(VkCommandBuffer commandBuffer, Framebuffer& framebuffer);
  void destroyFullscreenResources();
  void destroyPipelines();
  std::unique_ptr<Framebuffer> m_displayFramebuffer;
  std::unique_ptr<Framebuffer> m_sampleFramebuffer;
  // The two ping-pong accumulation images (RGBA32F, one pixel per output
  // pixel). m_accum[m_accumIndex] always holds the current running estimate;
  // the other image is the scratch target the next iteration renders into.
  // After each iteration m_accumIndex flips between 0 and 1.
  std::array<std::unique_ptr<Framebuffer>, 2> m_accum;
  int m_accumIndex = 0;
  // VkFramebuffer objects wrapping the two accumulation images, indexed the
  // same way as m_accum. Created once per resize rather than once per draw.
  std::array<resources::UniqueFramebuffer, 2> m_accumVkFramebuffers;
  // VkFramebuffer wrapping the caller's output image for this frame's tone
  // map. Recreated every frame because the caller may hand us a different
  // image each time; kept as a member so it stays alive until the GPU has
  // finished the submission that uses it.
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

  // --- Resources for the path-trace pass itself (pathTraceVolume.frag) ---
  //
  // Uniforms for the path-trace shader. Instead of one copy of the uniform
  // block that is rewritten before every iteration, this buffer is a "ring"
  // of `m_ptUniformCapacity` slots laid out back to back. Slot i holds the
  // uniforms for iteration i of the current frame (identical except for the
  // sample counters). Each draw selects its slot with a dynamic offset of
  // i * m_ptUniformStride bytes, so the whole frame can be recorded up front.
  resources::Buffer m_ptVolumeUniformBuffer;
  // CPU-visible pointer to the start of the ring. The buffer is kept mapped
  // for its whole lifetime so filling a slot is a plain memcpy.
  void* m_ptUniformMapped = nullptr;
  // Distance in bytes between consecutive slots: sizeof(PtVolumeUniforms)
  // rounded up to the device's required uniform-buffer offset alignment.
  VkDeviceSize m_ptUniformStride = 0;
  // Number of slots the ring currently has room for. Grown on demand when a
  // frame asks for more iterations than before; never shrunk.
  uint32_t m_ptUniformCapacity = 0;
  resources::UniqueDescriptorSetLayout m_ptVolumeDescriptorSetLayout;
  resources::UniqueDescriptorPool m_ptVolumeDescriptorPool;
  // Two descriptor sets, one per accumulation image. Set k is identical to
  // the other except that its "previous estimate" texture is m_accum[k]. An
  // iteration that reads m_accum[k] binds set k, so no descriptor needs to be
  // rewritten between iterations.
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
