#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gfxvulkan {

class Backend;

VkAccessFlags accessMaskForLayout(VkImageLayout layout);
VkPipelineStageFlags pipelineStageForLayout(VkImageLayout layout);

bool createBuffer(Backend& backend,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer,
                  VkDeviceMemory& memory);

bool createImage(Backend& backend,
                 uint32_t width,
                 uint32_t height,
                 uint32_t depth,
                 uint32_t arrayLayers,
                 VkFormat format,
                 VkImageType imageType,
                 VkImageUsageFlags usage,
                 VkImage& image,
                 VkDeviceMemory& memory);

bool createImageView(Backend& backend,
                     VkImage image,
                     VkFormat format,
                     VkImageViewType viewType,
                     VkImageAspectFlags aspect,
                     uint32_t layerCount,
                     VkImageView& view);

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
