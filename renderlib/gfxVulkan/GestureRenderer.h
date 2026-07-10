#pragma once

#include "gfxapi/IGestureRenderer.h"
#include "resources/Buffer.h"
#include "resources/SampledImage.h"

#include "glm.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class Font;

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
  bool ensureFontResources(const Font& font);
  bool ensureSelectionFramebuffer(int width, int height);
  bool ensureDisplayPipelines(VkFormat colorFormat);
  bool ensureSelectionPipelines();
  std::optional<resources::UniquePipeline> createPipeline(VkRenderPass renderPass, Topology topology);
  void uploadVerts(const void* data, size_t byteCount);
  void drawSequences(Framebuffer& target,
                     VkRenderPass renderPass,
                     const std::array<resources::UniquePipeline, kTopologyCount>& pipelines,
                     bool clearFirst,
                     SceneView& sceneView,
                     Gesture::Graphics& graphics,
                     const std::vector<int>& sequenceOrder,
                     int picking);
  bool ensureThickLinesResources();
  bool ensureThickLinesPipelines(VkFormat colorFormat);
  std::optional<resources::UniquePipeline> createThickLinesPipeline(VkRenderPass renderPass);
  void uploadStripVerts(const void* data, size_t byteCount);
  void drawStrips(Framebuffer& target,
                  VkRenderPass renderPass,
                  VkPipeline pipeline,
                  SceneView& sceneView,
                  Gesture::Graphics& graphics,
                  const std::vector<int>& sequenceOrder,
                  int picking);
  void drawImpl(SceneView& sceneView, Gesture::Graphics& graphics, const std::vector<int>& sequenceOrder);
  void destroy();

  Backend* m_backend = nullptr;

  int m_selectionWidth = 0;
  int m_selectionHeight = 0;
  std::unique_ptr<Framebuffer> m_selectionFbo;

  resources::Buffer m_vertexBuffer;
  VkDeviceSize m_vertexCapacity = 0;

  resources::Buffer m_uniformBuffer;

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
  resources::Buffer m_thickLinesUniformBuffer;
  resources::Buffer m_stripVertexBuffer;
  VkDeviceSize m_stripVertexCapacity = 0;
  resources::UniqueBufferView m_stripVertexView;
  resources::UniquePipeline m_thickLinesDisplayPipeline;
  resources::UniquePipeline m_thickLinesSelectionPipeline;
  VkFormat m_thickLinesDisplayColorFormat = VK_FORMAT_UNDEFINED;

  gfxApi::Framebuffer* m_target = nullptr;
};

} // namespace gfxvulkan
