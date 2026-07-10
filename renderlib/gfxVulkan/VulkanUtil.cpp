#include "VulkanUtil.h"

#include "Backend.h"

namespace gfxvulkan {

VkAccessFlags
accessMaskForLayout(VkImageLayout layout)
{
  switch (layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_ACCESS_SHADER_READ_BIT;
    default:
      return 0;
  }
}

VkPipelineStageFlags
pipelineStageForLayout(VkImageLayout layout)
{
  switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    default:
      return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }
}

void
transitionImageLayout(VkCommandBuffer commandBuffer,
                      VkImage image,
                      VkImageAspectFlags aspect,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      uint32_t layerCount)
{
  if (oldLayout == newLayout) {
    return;
  }

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = layerCount;
  barrier.srcAccessMask = accessMaskForLayout(oldLayout);
  barrier.dstAccessMask = accessMaskForLayout(newLayout);

  vkCmdPipelineBarrier(commandBuffer,
                       pipelineStageForLayout(oldLayout),
                       pipelineStageForLayout(newLayout),
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &barrier);
}

void
transitionImageLayout(Backend& backend,
                      VkImage image,
                      VkImageAspectFlags aspect,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      uint32_t layerCount)
{
  VkCommandBuffer commandBuffer = backend.beginSingleTimeCommands();
  transitionImageLayout(commandBuffer, image, aspect, oldLayout, newLayout, layerCount);
  backend.endSingleTimeCommands(commandBuffer);
}

void
copyBufferToImage(Backend& backend,
                  VkBuffer buffer,
                  VkImage image,
                  uint32_t width,
                  uint32_t height,
                  uint32_t depth,
                  uint32_t layerCount)
{
  VkCommandBuffer commandBuffer = backend.beginSingleTimeCommands();

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = layerCount;
  region.imageOffset = { 0, 0, 0 };
  region.imageExtent = { width, height, depth };

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  backend.endSingleTimeCommands(commandBuffer);
}

} // namespace gfxvulkan
