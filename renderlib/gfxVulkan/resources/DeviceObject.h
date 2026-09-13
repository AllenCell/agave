#pragma once

#include "ResourceRegistry.h"

#include <utility>

namespace gfxvulkan::resources {

template<typename Handle, typename Deleter, DestructionPhase Phase>
class DeviceObject
{
public:
  DeviceObject() = default;

  DeviceObject(ResourceRegistryPtr registry, Handle handle)
    : m_registry(std::move(registry))
    , m_handle(handle)
  {
    if (m_registry && m_handle != VK_NULL_HANDLE) {
      m_token = m_registry->track(Phase, [handle](VkDevice device) { Deleter{}(device, handle); });
      if (m_token == 0) {
        m_handle = VK_NULL_HANDLE;
      }
    }
  }

  ~DeviceObject() { reset(); }

  DeviceObject(DeviceObject&& other) noexcept
    : m_registry(std::move(other.m_registry))
    , m_handle(std::exchange(other.m_handle, VK_NULL_HANDLE))
    , m_token(std::exchange(other.m_token, 0))
  {
  }

  DeviceObject& operator=(DeviceObject&& other) noexcept
  {
    if (this != &other) {
      reset();
      m_registry = std::move(other.m_registry);
      m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
      m_token = std::exchange(other.m_token, 0);
    }
    return *this;
  }

  DeviceObject(const DeviceObject&) = delete;
  DeviceObject& operator=(const DeviceObject&) = delete;

  Handle get() const
  {
    return m_handle != VK_NULL_HANDLE && m_registry && m_registry->isAlive() ? m_handle : VK_NULL_HANDLE;
  }

  explicit operator bool() const { return get() != VK_NULL_HANDLE; }

  void reset()
  {
    if (m_token != 0 && m_registry) {
      m_registry->destroy(m_token);
    }
    m_token = 0;
    m_handle = VK_NULL_HANDLE;
    m_registry.reset();
  }

  // Stops registry ownership without destroying the Vulkan object. This is
  // intentionally explicit and should be reserved for APIs that transfer
  // ownership to another Vulkan owner.
  Handle release()
  {
    if (m_token != 0 && m_registry) {
      m_registry->forget(m_token);
    }
    m_token = 0;
    m_registry.reset();
    return std::exchange(m_handle, VK_NULL_HANDLE);
  }

private:
  ResourceRegistryPtr m_registry;
  Handle m_handle = VK_NULL_HANDLE;
  ResourceRegistry::Token m_token = 0;
};

#define GFXVULKAN_DEVICE_DELETER(Name, HandleType, Function)                                                           \
  struct Name                                                                                                          \
  {                                                                                                                    \
    void operator()(VkDevice device, HandleType handle) const { Function(device, handle, nullptr); }                   \
  }

GFXVULKAN_DEVICE_DELETER(BufferDeleter, VkBuffer, vkDestroyBuffer);
GFXVULKAN_DEVICE_DELETER(BufferViewDeleter, VkBufferView, vkDestroyBufferView);
GFXVULKAN_DEVICE_DELETER(ImageDeleter, VkImage, vkDestroyImage);
GFXVULKAN_DEVICE_DELETER(ImageViewDeleter, VkImageView, vkDestroyImageView);
GFXVULKAN_DEVICE_DELETER(SamplerDeleter, VkSampler, vkDestroySampler);
GFXVULKAN_DEVICE_DELETER(ShaderModuleDeleter, VkShaderModule, vkDestroyShaderModule);
GFXVULKAN_DEVICE_DELETER(FramebufferDeleter, VkFramebuffer, vkDestroyFramebuffer);
GFXVULKAN_DEVICE_DELETER(RenderPassDeleter, VkRenderPass, vkDestroyRenderPass);
GFXVULKAN_DEVICE_DELETER(DescriptorPoolDeleter, VkDescriptorPool, vkDestroyDescriptorPool);
GFXVULKAN_DEVICE_DELETER(DescriptorSetLayoutDeleter, VkDescriptorSetLayout, vkDestroyDescriptorSetLayout);
GFXVULKAN_DEVICE_DELETER(PipelineLayoutDeleter, VkPipelineLayout, vkDestroyPipelineLayout);
GFXVULKAN_DEVICE_DELETER(PipelineDeleter, VkPipeline, vkDestroyPipeline);
GFXVULKAN_DEVICE_DELETER(FenceDeleter, VkFence, vkDestroyFence);
GFXVULKAN_DEVICE_DELETER(CommandPoolDeleter, VkCommandPool, vkDestroyCommandPool);
GFXVULKAN_DEVICE_DELETER(SwapchainDeleter, VkSwapchainKHR, vkDestroySwapchainKHR);

struct DeviceMemoryDeleter
{
  void operator()(VkDevice device, VkDeviceMemory memory) const { vkFreeMemory(device, memory, nullptr); }
};

#undef GFXVULKAN_DEVICE_DELETER

using UniqueBuffer = DeviceObject<VkBuffer, BufferDeleter, DestructionPhase::Allocation>;
using UniqueBufferView = DeviceObject<VkBufferView, BufferViewDeleter, DestructionPhase::ViewAndSampler>;
using UniqueImage = DeviceObject<VkImage, ImageDeleter, DestructionPhase::Allocation>;
using UniqueImageView = DeviceObject<VkImageView, ImageViewDeleter, DestructionPhase::ViewAndSampler>;
using UniqueSampler = DeviceObject<VkSampler, SamplerDeleter, DestructionPhase::ViewAndSampler>;
using UniqueShaderModule = DeviceObject<VkShaderModule, ShaderModuleDeleter, DestructionPhase::ViewAndSampler>;
using UniqueFramebuffer = DeviceObject<VkFramebuffer, FramebufferDeleter, DestructionPhase::Framebuffer>;
using UniqueRenderPass = DeviceObject<VkRenderPass, RenderPassDeleter, DestructionPhase::RenderPass>;
using UniqueDescriptorPool = DeviceObject<VkDescriptorPool, DescriptorPoolDeleter, DestructionPhase::DescriptorPool>;
using UniqueDescriptorSetLayout =
  DeviceObject<VkDescriptorSetLayout, DescriptorSetLayoutDeleter, DestructionPhase::DescriptorSetLayout>;
using UniquePipelineLayout = DeviceObject<VkPipelineLayout, PipelineLayoutDeleter, DestructionPhase::PipelineLayout>;
using UniquePipeline = DeviceObject<VkPipeline, PipelineDeleter, DestructionPhase::Pipeline>;
using UniqueFence = DeviceObject<VkFence, FenceDeleter, DestructionPhase::Synchronization>;
using UniqueCommandPool = DeviceObject<VkCommandPool, CommandPoolDeleter, DestructionPhase::CommandPool>;
using UniqueSwapchain = DeviceObject<VkSwapchainKHR, SwapchainDeleter, DestructionPhase::Swapchain>;
using UniqueDeviceMemory = DeviceObject<VkDeviceMemory, DeviceMemoryDeleter, DestructionPhase::Memory>;

} // namespace gfxvulkan::resources
