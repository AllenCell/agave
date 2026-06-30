#pragma once

#include "gfxapi/Framebuffer.h"

#include <vulkan/vulkan.h>

namespace gfxvulkan {

class Backend;

class Framebuffer : public gfxApi::Framebuffer
{
public:
  Framebuffer(Backend& backend, const gfxApi::FramebufferDesc& desc);
  ~Framebuffer() override;

  void bind() override {}
  void release() override {}
  void resize(uint32_t width, uint32_t height) override;

  uint32_t width() const override { return m_width; }
  uint32_t height() const override { return m_height; }

  void clear(const gfxApi::ClearColor& color) override;
  void toImage(void* pixels) override;

  VkImage colorImage() const { return m_colorImage; }
  VkImageView colorImageView() const { return m_colorImageView; }
  VkFormat colorFormat() const { return m_colorFormat; }
  VkImageLayout colorLayout() const { return m_colorLayout; }

private:
  void destroy();
  void createImages();
  void createImage(VkFormat format,
                   VkImageUsageFlags usage,
                   VkImageAspectFlags aspect,
                   VkImage& image,
                   VkDeviceMemory& memory,
                   VkImageView& view);
  void transitionColorImage(VkCommandBuffer commandBuffer, VkImageLayout newLayout);
  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);

  Backend& m_backend;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  VkFormat m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  bool m_hasDepthStencil = false;

  VkImage m_colorImage = VK_NULL_HANDLE;
  VkDeviceMemory m_colorMemory = VK_NULL_HANDLE;
  VkImageView m_colorImageView = VK_NULL_HANDLE;
  VkImageLayout m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
  VkImageView m_depthImageView = VK_NULL_HANDLE;
};

} // namespace gfxvulkan
