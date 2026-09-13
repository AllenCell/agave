#pragma once

#include "AppScene.h"
#include "Status.h"
#include "VolumeTextureVk.h"
#include "gfxapi/IRenderWindow.h"
#include "resources/Buffer.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <memory>
#include <string>

class RenderSettings;

namespace gfxvulkan {

class Backend;
class Framebuffer;

class RenderVk : public gfxApi::IRenderWindow
{
public:
  static const std::string TYPE_NAME;

  RenderVk(Backend& backend, RenderSettings* renderSettings);
  ~RenderVk() override;

  void initialize(uint32_t w, uint32_t h) override;
  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void getSize(uint32_t& w, uint32_t& h) override;
  void cleanUpResources() override;

  std::shared_ptr<CStatus> getStatusInterface() override { return m_status; }

  RenderSettings& renderSettings() override;
  Scene* scene() override;
  void setScene(Scene* s) override;

protected:
  virtual VolumeTextureMode volumeTextureMode() const;
  virtual bool usesProgressiveAccumulation() const;
  virtual float volumeShaderMode() const;
  virtual float rayStepCount() const;
  void renderToFramebuffer(const CCamera& camera, Framebuffer& framebuffer);

  bool prepareToRender();
  bool ensureFrameResources();
  bool ensurePipeline(VkFormat colorFormat);
  bool updateDescriptorSet();
  bool updateUniformBuffer(const CCamera& camera);
  void destroyFrameResources();
  void destroyPipeline();
  // Volume/colormap GPU textures, owned by the base renderer but needed by the
  // path-trace subclass to bind into its own shading pipeline.
  const VolumeTextureVk& volumeTexture() const { return m_volume; }

  Backend& m_backend;
  RenderSettings* m_renderSettings = nullptr;
  Scene* m_scene = nullptr;
  std::shared_ptr<CStatus> m_status;
  uint32_t m_w = 0;
  uint32_t m_h = 0;

private:
  gfxApi::ClearColor backgroundClearColor() const;

  VolumeTextureVk m_volume;
  size_t m_gpuBytes = 0;

  resources::Buffer m_vertexBuffer;
  resources::Buffer m_indexBuffer;
  uint32_t m_indexCount = 0;
  resources::Buffer m_uniformBuffer;

  resources::UniqueDescriptorSetLayout m_descriptorSetLayout;
  resources::UniqueDescriptorPool m_descriptorPool;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
  resources::UniqueRenderPass m_renderPass;
  resources::UniquePipelineLayout m_pipelineLayout;
  resources::UniquePipeline m_pipeline;
  VkFormat m_pipelineColorFormat = VK_FORMAT_UNDEFINED;

  std::unique_ptr<Framebuffer> m_internalFramebuffer;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
};

} // namespace gfxvulkan
