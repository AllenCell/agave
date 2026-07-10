#pragma once

#include "gfxapi/IGraphicsDevice.h"
#include "resources/Buffer.h"
#include "resources/Image.h"
#include "resources/ResourceRegistry.h"
#include "resources/DeviceObject.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gfxvulkan {

class Device : public gfxApi::IGraphicsDevice
{
public:
  Device();
  ~Device() override;

  void initialize(VkPhysicalDevice physicalDevice, VkDevice device);
  void release();

  gfxApi::BackendKind backend() const override { return gfxApi::BackendKind::Vulkan; }

  gfxApi::ShaderHandle createShader(const gfxApi::ShaderDesc& desc) override;
  void destroyShader(gfxApi::ShaderHandle handle) override;

  gfxApi::ShaderProgramHandle createShaderProgram(const gfxApi::ShaderProgramDesc& desc) override;
  void destroyShaderProgram(gfxApi::ShaderProgramHandle handle) override;

  VkShaderModule shaderModule(gfxApi::ShaderHandle handle) const;
  gfxApi::ShaderStage shaderStage(gfxApi::ShaderHandle handle) const;

  std::optional<resources::Buffer> createBuffer(VkDeviceSize size,
                                                VkBufferUsageFlags usage,
                                                VkMemoryPropertyFlags properties);
  std::optional<resources::Image> createImage(uint32_t width,
                                              uint32_t height,
                                              uint32_t depth,
                                              uint32_t arrayLayers,
                                              VkFormat format,
                                              VkImageType imageType,
                                              VkImageUsageFlags usage);
  std::optional<resources::UniqueImageView> createImageView(VkImage image,
                                                            VkFormat format,
                                                            VkImageViewType viewType,
                                                            VkImageAspectFlags aspect,
                                                            uint32_t layerCount);
  std::optional<resources::UniqueBufferView> createBufferView(const VkBufferViewCreateInfo& createInfo);
  std::optional<resources::UniqueSampler> createSampler(const VkSamplerCreateInfo& createInfo);
  std::optional<resources::UniqueShaderModule> createShaderModule(const uint32_t* words, size_t wordCount);
  std::optional<resources::UniqueDescriptorSetLayout> createDescriptorSetLayout(
    const VkDescriptorSetLayoutCreateInfo& createInfo);
  std::optional<resources::UniqueDescriptorPool> createDescriptorPool(
    const VkDescriptorPoolCreateInfo& createInfo);
  std::optional<resources::UniquePipelineLayout> createPipelineLayout(
    const VkPipelineLayoutCreateInfo& createInfo);
  std::optional<resources::UniqueRenderPass> createRenderPass(const VkRenderPassCreateInfo& createInfo);
  std::optional<resources::UniquePipeline> createPipeline(const VkGraphicsPipelineCreateInfo& createInfo,
                                                           VkPipelineCache cache = VK_NULL_HANDLE);
  std::optional<resources::UniqueFramebuffer> createFramebuffer(const VkFramebufferCreateInfo& createInfo);
  std::optional<resources::UniqueFence> createFence(const VkFenceCreateInfo& createInfo);
  std::optional<resources::UniqueCommandPool> createCommandPool(const VkCommandPoolCreateInfo& createInfo);
  std::optional<resources::UniqueSwapchain> createSwapchain(const VkSwapchainCreateInfoKHR& createInfo);

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
  size_t trackedResourceCount() const;

private:
  struct ShaderRecord
  {
    resources::UniqueShaderModule module;
    gfxApi::ShaderStage stage = gfxApi::ShaderStage::Vertex;
  };

  struct ShaderProgramRecord
  {
    std::vector<gfxApi::ShaderHandle> shaders;
  };

  uint64_t m_nextId = 1;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  resources::ResourceRegistryPtr m_resources;
  std::unordered_map<uint64_t, ShaderRecord> m_shaders;
  std::unordered_map<uint64_t, ShaderProgramRecord> m_programs;
};

} // namespace gfxvulkan
