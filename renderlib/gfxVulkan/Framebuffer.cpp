#include "Framebuffer.h"

#include "Backend.h"
#include "Device.h"
#include "Logging.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gfxvulkan {

namespace {

VkFormat
toVkFormat(gfxApi::FramebufferColorFormat format)
{
  switch (format) {
    case gfxApi::FramebufferColorFormat::Rgba8:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case gfxApi::FramebufferColorFormat::Rgba32F:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  return VK_FORMAT_R8G8B8A8_UNORM;
}

VkPipelineStageFlags
stageForLayout(VkImageLayout layout)
{
  switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    default:
      return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }
}

VkAccessFlags
accessForLayout(VkImageLayout layout)
{
  switch (layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return 0;
    default:
      return 0;
  }
}

} // namespace

Framebuffer::Framebuffer(Backend& backend, const gfxApi::FramebufferDesc& desc)
  : m_backend(backend)
  , m_colorFormat(toVkFormat(desc.colorFormat))
  , m_hasDepthStencil(desc.depthStencil)
{
  resize(desc.width, desc.height);
}

Framebuffer::Framebuffer(Backend& backend,
                         uint32_t width,
                         uint32_t height,
                         VkFormat colorFormat,
                         VkImage colorImage,
                         VkImageLayout initialLayout)
  : m_backend(backend)
  , m_width(width)
  , m_height(height)
  , m_colorFormat(colorFormat)
  , m_externalColorImage(colorImage)
  , m_colorLayout(initialLayout)
  , m_ownsColorImage(false)
{
  if (m_externalColorImage != VK_NULL_HANDLE && m_width > 0 && m_height > 0) {
    auto view = m_backend.device().createImageView(
      m_externalColorImage, m_colorFormat, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (view) {
      m_colorImageView = std::move(*view);
    }
  }
}

Framebuffer::~Framebuffer()
{
  destroy();
}

void
Framebuffer::resize(uint32_t width, uint32_t height)
{
  if (!m_ownsColorImage) {
    LOG_ERROR << "Cannot resize a Vulkan framebuffer that wraps an externally owned image";
    return;
  }

  if (width == m_width && height == m_height) {
    return;
  }

  destroy();
  m_width = width;
  m_height = height;
  if (m_width == 0 || m_height == 0) {
    return;
  }
  createImages();
}

void
Framebuffer::createImages()
{
  auto color = m_backend.device().createImage(m_width,
                                              m_height,
                                              1,
                                              1,
                                              m_colorFormat,
                                              VK_IMAGE_TYPE_2D,
                                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  if (!color) {
    return;
  }
  auto colorView = m_backend.device().createImageView(
    color->get(), m_colorFormat, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  if (!colorView) {
    return;
  }
  m_colorAllocation = std::move(*color);
  m_colorImageView = std::move(*colorView);
  m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (m_hasDepthStencil) {
    auto depth =
      m_backend.device().createImage(m_width,
                                     m_height,
                                     1,
                                     1,
                                     VK_FORMAT_D32_SFLOAT,
                                     VK_IMAGE_TYPE_2D,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (!depth) {
      return;
    }
    auto depthView = m_backend.device().createImageView(
      depth->get(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    if (!depthView) {
      return;
    }
    m_depthAllocation = std::move(*depth);
    m_depthImageView = std::move(*depthView);
  }
}

void
Framebuffer::destroy()
{
  m_depthImageView.reset();
  m_depthAllocation.reset();
  m_colorImageView.reset();
  m_colorAllocation.reset();
  m_externalColorImage = VK_NULL_HANDLE;

  m_width = 0;
  m_height = 0;
  m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void
Framebuffer::transitionColorImage(VkCommandBuffer commandBuffer, VkImageLayout newLayout)
{
  if (m_colorLayout == newLayout) {
    return;
  }

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = m_colorLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = colorImage();
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = accessForLayout(m_colorLayout);
  barrier.dstAccessMask = accessForLayout(newLayout);

  vkCmdPipelineBarrier(
    commandBuffer, stageForLayout(m_colorLayout), stageForLayout(newLayout), 0, 0, nullptr, 0, nullptr, 1, &barrier);
  m_colorLayout = newLayout;
}

void
Framebuffer::clear(const gfxApi::ClearColor& color)
{
  if (colorImage() == VK_NULL_HANDLE) {
    return;
  }

  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkClearColorValue clearColor = { { color.r, color.g, color.b, color.a } };
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = 1;
  range.baseArrayLayer = 0;
  range.layerCount = 1;
  vkCmdClearColorImage(commandBuffer, colorImage(), m_colorLayout, &clearColor, 1, &range);

  m_backend.endSingleTimeCommands(commandBuffer);
}

void
Framebuffer::toImage(void* pixels)
{
  if (!pixels || colorImage() == VK_NULL_HANDLE || m_width == 0 || m_height == 0) {
    return;
  }

  if (m_colorFormat != VK_FORMAT_R8G8B8A8_UNORM) {
    LOG_ERROR << "Vulkan Framebuffer::toImage currently supports only RGBA8 framebuffers";
    return;
  }

  const VkDeviceSize byteCount = static_cast<VkDeviceSize>(m_width) * static_cast<VkDeviceSize>(m_height) * 4;
  auto staging =
    m_backend.device().createBuffer(byteCount,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return;
  }

  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  VkBufferImageCopy copyRegion = {};
  copyRegion.bufferOffset = 0;
  copyRegion.bufferRowLength = 0;
  copyRegion.bufferImageHeight = 0;
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.mipLevel = 0;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageOffset = { 0, 0, 0 };
  copyRegion.imageExtent = { m_width, m_height, 1 };

  vkCmdCopyImageToBuffer(commandBuffer, colorImage(), m_colorLayout, staging->get(), 1, &copyRegion);
  m_backend.endSingleTimeCommands(commandBuffer);

  void* mapped = nullptr;
  VkDevice device = m_backend.logicalDevice();
  if (vkMapMemory(device, staging->memory(), 0, byteCount, 0, &mapped) != VK_SUCCESS) {
    return;
  }

  const auto* src = static_cast<const uint8_t*>(mapped);
  auto* dst = static_cast<uint8_t*>(pixels);
  for (uint32_t i = 0; i < m_width * m_height; ++i) {
    dst[i * 4 + 0] = src[i * 4 + 2];
    dst[i * 4 + 1] = src[i * 4 + 1];
    dst[i * 4 + 2] = src[i * 4 + 0];
    dst[i * 4 + 3] = src[i * 4 + 3];
  }

  vkUnmapMemory(device, staging->memory());
}

} // namespace gfxvulkan
