#pragma once

#include "gfxapi/Framebuffer.h"
#include "resources/Buffer.h"
#include "resources/Image.h"

#include <vulkan/vulkan.h>

namespace gfxvulkan {

class Backend;

class Framebuffer : public gfxApi::Framebuffer
{
public:
  Framebuffer(Backend& backend, const gfxApi::FramebufferDesc& desc);
  Framebuffer(Backend& backend,
              uint32_t width,
              uint32_t height,
              VkFormat colorFormat,
              VkImage colorImage,
              VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED);
  ~Framebuffer() override;

  void bind() override {}
  void release() override {}
  void resize(uint32_t width, uint32_t height) override;

  uint32_t width() const override { return m_width; }
  uint32_t height() const override { return m_height; }

  void clear(const gfxApi::ClearColor& color) override;
  void toImage(void* pixels) override;

  VkImage colorImage() const { return m_ownsColorImage ? m_colorAllocation.get() : m_externalColorImage; }
  VkImageView colorImageView() const { return m_colorImageView.get(); }
  VkFormat colorFormat() const { return m_colorFormat; }
  VkImageLayout colorLayout() const { return m_colorLayout; }
  void transitionColorImage(VkCommandBuffer commandBuffer, VkImageLayout newLayout);

private:
  void destroy();
  void createImages();

  Backend& m_backend;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  VkFormat m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  bool m_hasDepthStencil = false;

  resources::Image m_colorAllocation;
  VkImage m_externalColorImage = VK_NULL_HANDLE;
  resources::UniqueImageView m_colorImageView;
  VkImageLayout m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool m_ownsColorImage = true;

  resources::Image m_depthAllocation;
  resources::UniqueImageView m_depthImageView;
};

} // namespace gfxvulkan
