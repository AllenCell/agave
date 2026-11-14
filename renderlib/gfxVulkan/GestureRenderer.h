#pragma once

#include "gfxapi/IGestureRenderer.h"
#include "resources/Buffer.h"
#include "resources/SampledImage.h"
#include "resources/UniformRing.h"

#include "glm.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class AgaveFont;

namespace gfxApi {
class Framebuffer;
}

namespace gfxvulkan {

class Backend;
class Framebuffer;

// Vulkan gesture/manipulator renderer. Draws the gizmo geometry emitted by the
// tools (lines/triangles/points via the gui shader) as an overlay on the target
// framebuffer, and renders per-handle selection codes into an offscreen
// selection buffer that pick() reads back. Also draws thick-line strips
// (emitted by tools such as the translate manipulator) via a separate pipeline
// that expands each segment into a screen-space quad in the vertex shader.
//
// line/triangle/point path plus thick-line strips is implemented.
class GestureRenderer : public gfxApi::IGestureRenderer
{
public:
  GestureRenderer();
  ~GestureRenderer() override;

  bool selectionBufferMatches(int width, int height) const override;
  bool updateSelectionBuffer(int width, int height) override;
  void clearSelectionBuffer() override;

  bool pick(const Gesture::Input& input, const SceneView::Viewport& viewport, uint32_t& selectionCode) override;

  void drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics) override;
  void draw(SceneView& sceneView, Gesture::Graphics& graphics) override;

  // The framebuffer that the overlay is composited onto. Set each frame by the
  // Vulkan render path before draw()/drawUnderlay(); ignored by other backends.
  void setTargetFramebuffer(gfxApi::Framebuffer* target) override;

  enum Topology
  {
    kTri = 0,
    kLine = 1,
    kPoint = 2,
    kTopologyCount = 3
  };

private:
  bool ensureBackend();
  bool ensureCommonResources();
  bool ensureFontResources(const AgaveFont& font);
  bool ensureSelectionFramebuffer(int width, int height);
  bool ensureDisplayPipelines(VkFormat colorFormat);
  bool ensureSelectionPipelines();
  std::optional<resources::UniquePipeline> createPipeline(VkRenderPass renderPass, Topology topology);
  void uploadVerts(const void* data, size_t byteCount);
  // Record the gui-shader draws for the sequences in sequenceOrder. Consumes one
  // uniform ring slot per sequence, starting at uniformSlot.
  void recordSequences(VkCommandBuffer commandBuffer,
                       const std::array<resources::UniquePipeline, kTopologyCount>& pipelines,
                       SceneView& sceneView,
                       Gesture::Graphics& graphics,
                       const std::vector<int>& sequenceOrder,
                       int picking,
                       uint32_t& uniformSlot);
  bool ensureThickLinesResources();
  bool ensureThickLinesPipelines(VkFormat colorFormat, bool includeSelection);
  std::optional<resources::UniquePipeline> createThickLinesPipeline(VkRenderPass renderPass);
  void uploadStripVerts(const void* data, size_t byteCount);
  // Record the thick-line draws for the sequences in sequenceOrder. Consumes one
  // uniform ring slot per strip, starting at uniformSlot.
  void recordStrips(VkCommandBuffer commandBuffer,
                    VkPipeline pipeline,
                    SceneView& sceneView,
                    Gesture::Graphics& graphics,
                    const std::vector<int>& sequenceOrder,
                    int picking,
                    uint32_t& uniformSlot);
  // Draw the complete gesture overlay for one framebuffer and submit it as a
  // single piece of GPU work.
  //
  // Everything for the target -- the optional clear, the gui-shader draws for
  // every sequence, and the thick-line draws for every strip -- is recorded
  // into ONE command buffer inside ONE render pass instance, then submitted
  // once. That matters because each submission here allocates a command
  // buffer, creates a fence, and blocks until the GPU drains it, so the old
  // one-submit-per-sequence/per-strip approach cost tens of full GPU stalls to
  // draw a handful of lines. The draws are recorded in the order
  // sequences-then-strips and rasterization order is preserved within a render
  // pass, so blending still composites in the same order as before.
  //
  // Callers must have sized the uniform rings for this frame (see
  // ensureUniformSlots) before calling: the recorded draws each read their own
  // ring slot, which is only safe while they share one submission.
  //
  //   target         framebuffer to composite into. Transitioned to
  //                  COLOR_ATTACHMENT_OPTIMAL here and left in that layout.
  //   renderPass     render pass matching target's color format. Must be the
  //                  one the supplied pipelines were created against; it loads
  //                  existing contents, so anything already in the target (the
  //                  volume render, an earlier underlay) survives.
  //   pipelines      the three topology variants (triangles/lines/points) for
  //                  the gui shader, or nullptr to skip the sequence draws --
  //                  e.g. a frame that emitted only thick-line strips.
  //   stripPipeline  thick-line pipeline, or VK_NULL_HANDLE to skip the strip
  //                  draws. Must belong to renderPass as well.
  //   clearFirst     clear the target to the "no selection" code before
  //                  drawing. Only the selection buffer wants this; the display
  //                  target must keep what is already there.
  //   sceneView      supplies the camera (3d sequences) and viewport (2d
  //                  screen-space sequences and thick-line screen expansion).
  //   graphics       the geometry to draw: vertex ranges per sequence and the
  //                  strip ranges/thicknesses. Not cleared here; the caller
  //                  decides when a frame's commands are consumed, because the
  //                  same geometry is drawn again into the selection buffer.
  //   sequenceOrder  which command sequences to draw, in back-to-front order.
  //                  drawUnderlay passes just the underlay sequence; draw()
  //                  passes the depth-tested, stacked and 2d screen sequences.
  //   picking        0 draws the visible overlay, 1 draws per-handle selection
  //                  codes instead of colors (what pick() reads back).
  void drawTarget(Framebuffer& target,
                  VkRenderPass renderPass,
                  const std::array<resources::UniquePipeline, kTopologyCount>* pipelines,
                  VkPipeline stripPipeline,
                  bool clearFirst,
                  SceneView& sceneView,
                  Gesture::Graphics& graphics,
                  const std::vector<int>& sequenceOrder,
                  int picking);
  // Size the host-visible uniform rings so a frame's draws each get their own
  // slot, addressed with a dynamic descriptor offset.
  bool ensureUniformSlots(uint32_t sequenceSlots, uint32_t stripSlots);
  void drawImpl(SceneView& sceneView, Gesture::Graphics& graphics, const std::vector<int>& sequenceOrder);
  void destroy();

  Backend* m_backend = nullptr;

  int m_selectionWidth = 0;
  int m_selectionHeight = 0;
  std::unique_ptr<Framebuffer> m_selectionFbo;

  resources::Buffer m_vertexBuffer;
  VkDeviceSize m_vertexCapacity = 0;

  resources::UniformRing m_guiUniforms;

  resources::SampledImage m_dummyTexture;

  // Font atlas texture. Created lazily on the first draw call once
  // Gesture::Graphics::font has been loaded. Replaces the dummy sampler in
  // the descriptor set so the gui shader can composite text glyphs.
  resources::SampledImage m_fontTexture;
  uint32_t m_fontWidth = 0;
  uint32_t m_fontHeight = 0;

  resources::UniqueDescriptorSetLayout m_descriptorSetLayout;
  resources::UniqueDescriptorPool m_descriptorPool;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
  resources::UniquePipelineLayout m_pipelineLayout;

  resources::UniqueRenderPass m_displayRenderPass;
  VkFormat m_displayColorFormat = VK_FORMAT_UNDEFINED;
  std::array<resources::UniquePipeline, kTopologyCount> m_displayPipelines;

  resources::UniqueRenderPass m_selectionRenderPass;
  std::array<resources::UniquePipeline, kTopologyCount> m_selectionPipelines;

  // Thick-line strip pipeline. Vertex data is uploaded to a separate
  // uniform-texel buffer that the shader indexes per triangle to expand each
  // line segment into a screen-space quad with mitered ends.
  resources::UniqueDescriptorSetLayout m_thickLinesDescriptorSetLayout;
  resources::UniqueDescriptorPool m_thickLinesDescriptorPool;
  VkDescriptorSet m_thickLinesDescriptorSet = VK_NULL_HANDLE;
  resources::UniquePipelineLayout m_thickLinesPipelineLayout;
  resources::UniformRing m_thickLinesUniforms;
  resources::Buffer m_stripVertexBuffer;
  VkDeviceSize m_stripVertexCapacity = 0;
  resources::UniqueBufferView m_stripVertexView;
  resources::UniquePipeline m_thickLinesDisplayPipeline;
  resources::UniquePipeline m_thickLinesSelectionPipeline;
  VkFormat m_thickLinesDisplayColorFormat = VK_FORMAT_UNDEFINED;

  gfxApi::Framebuffer* m_target = nullptr;
};

} // namespace gfxvulkan
