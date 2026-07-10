#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gfxvulkan {

class Backend;

VkAccessFlags accessMaskForLayout(VkImageLayout layout);
VkPipelineStageFlags pipelineStageForLayout(VkImageLayout layout);

void transitionImageLayout(VkCommandBuffer commandBuffer,
                           VkImage image,
                           VkImageAspectFlags aspect,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           uint32_t layerCount = 1);

void transitionImageLayout(Backend& backend,
                           VkImage image,
                           VkImageAspectFlags aspect,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           uint32_t layerCount = 1);

void copyBufferToImage(Backend& backend,
                       VkBuffer buffer,
                       VkImage image,
                       uint32_t width,
                       uint32_t height,
                       uint32_t depth,
                       uint32_t layerCount = 1);

} // namespace gfxvulkan
