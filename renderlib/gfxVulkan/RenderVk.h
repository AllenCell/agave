#pragma once

#include "AppScene.h"
#include "Status.h"
#include "VolumeTextureVk.h"
#include "gfxapi/IRenderWindow.h"

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
  VkShaderModule createShaderModule(const uint32_t* words, size_t wordCount) const;

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

  VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
  VkBuffer m_indexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
  uint32_t m_indexCount = 0;
  VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_uniformMemory = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkFormat m_pipelineColorFormat = VK_FORMAT_UNDEFINED;

  std::unique_ptr<Framebuffer> m_internalFramebuffer;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
};

} // namespace gfxvulkan
