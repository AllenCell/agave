#include "Framebuffer.h"

#include "Backend.h"
#include "Logging.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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
  , m_colorImage(colorImage)
  , m_colorLayout(initialLayout)
  , m_ownsColorImage(false)
  , m_ownsColorMemory(false)
{
  if (m_colorImage != VK_NULL_HANDLE && m_width > 0 && m_height > 0) {
    createImageView(m_colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, m_colorImage, m_colorImageView);
  }
}

Framebuffer::~Framebuffer()
{
  destroy();
}

void
Framebuffer::resize(uint32_t width, uint32_t height)
{
  if (!m_ownsColorImage || !m_ownsColorMemory) {
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
  createImage(m_colorFormat,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT,
              m_colorImage,
              m_colorMemory,
              m_colorImageView);
  m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (m_hasDepthStencil) {
    createImage(VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                m_depthImage,
                m_depthMemory,
                m_depthImageView);
  }
}

void
Framebuffer::createImage(VkFormat format,
                         VkImageUsageFlags usage,
                         VkImageAspectFlags aspect,
                         VkImage& image,
                         VkDeviceMemory& memory,
                         VkImageView& view)
{
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = m_width;
  imageInfo.extent.height = m_height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkDevice device = m_backend.logicalDevice();
  VkResult result = vkCreateImage(device, &imageInfo, nullptr, &image);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateImage failed with VkResult " << result;
    return;
  }

  VkMemoryRequirements memoryRequirements = {};
  vkGetImageMemoryRequirements(device, image, &memoryRequirements);

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = memoryRequirements.size;
  allocateInfo.memoryTypeIndex =
    m_backend.findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (allocateInfo.memoryTypeIndex == UINT32_MAX) {
    return;
  }

  result = vkAllocateMemory(device, &allocateInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkAllocateMemory for framebuffer image failed with VkResult " << result;
    return;
  }

  vkBindImageMemory(device, image, memory, 0);

  createImageView(format, aspect, image, view);
}

void
Framebuffer::createImageView(VkFormat format, VkImageAspectFlags aspect, VkImage image, VkImageView& view)
{
  if (image == VK_NULL_HANDLE) {
    return;
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkDevice device = m_backend.logicalDevice();
  VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &view);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateImageView failed with VkResult " << result;
  }
}

void
Framebuffer::destroy()
{
  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, m_depthImageView, nullptr);
    m_depthImageView = VK_NULL_HANDLE;
  }
  if (m_depthImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, m_depthImage, nullptr);
    m_depthImage = VK_NULL_HANDLE;
  }
  if (m_depthMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_depthMemory, nullptr);
    m_depthMemory = VK_NULL_HANDLE;
  }

  if (m_colorImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, m_colorImageView, nullptr);
    m_colorImageView = VK_NULL_HANDLE;
  }
  if (m_colorImage != VK_NULL_HANDLE && m_ownsColorImage) {
    vkDestroyImage(device, m_colorImage, nullptr);
  }
  m_colorImage = VK_NULL_HANDLE;
  if (m_colorMemory != VK_NULL_HANDLE && m_ownsColorMemory) {
    vkFreeMemory(device, m_colorMemory, nullptr);
  }
  m_colorMemory = VK_NULL_HANDLE;

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
  barrier.image = m_colorImage;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = accessForLayout(m_colorLayout);
  barrier.dstAccessMask = accessForLayout(newLayout);

  vkCmdPipelineBarrier(commandBuffer,
                       stageForLayout(m_colorLayout),
                       stageForLayout(newLayout),
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &barrier);
  m_colorLayout = newLayout;
}

void
Framebuffer::clear(const gfxApi::ClearColor& color)
{
  if (m_colorImage == VK_NULL_HANDLE) {
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
  vkCmdClearColorImage(commandBuffer, m_colorImage, m_colorLayout, &clearColor, 1, &range);

  m_backend.endSingleTimeCommands(commandBuffer);
}

void
Framebuffer::createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer& buffer,
                          VkDeviceMemory& memory)
{
  VkDevice device = m_backend.logicalDevice();
  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateBuffer failed with VkResult " << result;
    return;
  }

  VkMemoryRequirements memoryRequirements = {};
  vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = memoryRequirements.size;
  allocateInfo.memoryTypeIndex = m_backend.findMemoryType(memoryRequirements.memoryTypeBits, properties);
  if (allocateInfo.memoryTypeIndex == UINT32_MAX) {
    return;
  }

  result = vkAllocateMemory(device, &allocateInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkAllocateMemory for framebuffer buffer failed with VkResult " << result;
    return;
  }

  vkBindBufferMemory(device, buffer, memory, 0);
}

void
Framebuffer::toImage(void* pixels)
{
  if (!pixels || m_colorImage == VK_NULL_HANDLE || m_width == 0 || m_height == 0) {
    return;
  }

  if (m_colorFormat != VK_FORMAT_R8G8B8A8_UNORM) {
    LOG_ERROR << "Vulkan Framebuffer::toImage currently supports only RGBA8 framebuffers";
    return;
  }

  const VkDeviceSize byteCount = static_cast<VkDeviceSize>(m_width) * static_cast<VkDeviceSize>(m_height) * 4;
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  createBuffer(byteCount,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer,
               stagingMemory);
  if (stagingBuffer == VK_NULL_HANDLE || stagingMemory == VK_NULL_HANDLE) {
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

  vkCmdCopyImageToBuffer(commandBuffer, m_colorImage, m_colorLayout, stagingBuffer, 1, &copyRegion);
  m_backend.endSingleTimeCommands(commandBuffer);

  void* mapped = nullptr;
  VkDevice device = m_backend.logicalDevice();
  vkMapMemory(device, stagingMemory, 0, byteCount, 0, &mapped);

  const auto* src = static_cast<const uint8_t*>(mapped);
  auto* dst = static_cast<uint8_t*>(pixels);
  for (uint32_t i = 0; i < m_width * m_height; ++i) {
    dst[i * 4 + 0] = src[i * 4 + 2];
    dst[i * 4 + 1] = src[i * 4 + 1];
    dst[i * 4 + 2] = src[i * 4 + 0];
    dst[i * 4 + 3] = src[i * 4 + 3];
  }

  vkUnmapMemory(device, stagingMemory);
  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingMemory, nullptr);
}

} // namespace gfxvulkan
