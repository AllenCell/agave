#pragma once

#include "gfxapi/IGestureRenderer.h"

#include "glm.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

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
// NOTE: textured GUI/font elements are not handled yet; only the plain
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
  bool ensureSelectionFramebuffer(int width, int height);
  bool ensureDisplayPipelines(VkFormat colorFormat);
  bool ensureSelectionPipelines();
  VkPipeline createPipeline(VkRenderPass renderPass, Topology topology);
  void uploadVerts(const void* data, size_t byteCount);
  void drawSequences(Framebuffer& target,
                     VkRenderPass renderPass,
                     const std::array<VkPipeline, kTopologyCount>& pipelines,
                     bool clearFirst,
                     SceneView& sceneView,
                     Gesture::Graphics& graphics,
                     const std::vector<int>& sequenceOrder,
                     int picking);
  bool ensureThickLinesResources();
  bool ensureThickLinesPipelines(VkFormat colorFormat);
  VkPipeline createThickLinesPipeline(VkRenderPass renderPass);
  void uploadStripVerts(const void* data, size_t byteCount);
  void drawStrips(Framebuffer& target,
                  VkRenderPass renderPass,
                  VkPipeline pipeline,
                  SceneView& sceneView,
                  Gesture::Graphics& graphics,
                  int picking);
  VkShaderModule createShaderModule(const uint32_t* words, size_t wordCount) const;
  void drawImpl(SceneView& sceneView, Gesture::Graphics& graphics, const std::vector<int>& sequenceOrder);
  void destroy();

  Backend* m_backend = nullptr;

  int m_selectionWidth = 0;
  int m_selectionHeight = 0;
  std::unique_ptr<Framebuffer> m_selectionFbo;

  VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
  VkDeviceSize m_vertexCapacity = 0;

  VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_uniformMemory = VK_NULL_HANDLE;

  VkImage m_dummyImage = VK_NULL_HANDLE;
  VkDeviceMemory m_dummyMemory = VK_NULL_HANDLE;
  VkImageView m_dummyView = VK_NULL_HANDLE;
  VkSampler m_dummySampler = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

  VkRenderPass m_displayRenderPass = VK_NULL_HANDLE;
  VkFormat m_displayColorFormat = VK_FORMAT_UNDEFINED;
  std::array<VkPipeline, kTopologyCount> m_displayPipelines = {};

  VkRenderPass m_selectionRenderPass = VK_NULL_HANDLE;
  std::array<VkPipeline, kTopologyCount> m_selectionPipelines = {};

  // Thick-line strip pipeline. Vertex data is uploaded to a separate
  // uniform-texel buffer that the shader indexes per triangle to expand each
  // line segment into a screen-space quad with mitered ends.
  VkDescriptorSetLayout m_thickLinesDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_thickLinesDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_thickLinesDescriptorSet = VK_NULL_HANDLE;
  VkPipelineLayout m_thickLinesPipelineLayout = VK_NULL_HANDLE;
  VkBuffer m_thickLinesUniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_thickLinesUniformMemory = VK_NULL_HANDLE;
  VkBuffer m_stripVertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_stripVertexMemory = VK_NULL_HANDLE;
  VkDeviceSize m_stripVertexCapacity = 0;
  VkBufferView m_stripVertexView = VK_NULL_HANDLE;
  VkPipeline m_thickLinesDisplayPipeline = VK_NULL_HANDLE;
  VkPipeline m_thickLinesSelectionPipeline = VK_NULL_HANDLE;
  VkFormat m_thickLinesDisplayColorFormat = VK_FORMAT_UNDEFINED;

  gfxApi::Framebuffer* m_target = nullptr;
};

} // namespace gfxvulkan
