#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gfxvulkan {

class Backend;

VkAccessFlags
accessMaskForLayout(VkImageLayout layout);
VkPipelineStageFlags
pipelineStageForLayout(VkImageLayout layout);

void
transitionImageLayout(VkCommandBuffer commandBuffer,
                      VkImage image,
                      VkImageAspectFlags aspect,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      uint32_t layerCount = 1);

void
transitionImageLayout(Backend& backend,
                      VkImage image,
                      VkImageAspectFlags aspect,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      uint32_t layerCount = 1);

void
copyBufferToImage(Backend& backend,
                  VkBuffer buffer,
                  VkImage image,
                  uint32_t width,
                  uint32_t height,
                  uint32_t depth,
                  uint32_t layerCount = 1);

// Record into an existing command buffer instead of submitting one of its own.
// The Backend& overloads above each begin, submit and then wait for the GPU to
// go idle, so a transition-copy-transition sequence costs three full pipeline
// stalls. Recording all three into one command buffer and submitting once costs
// none.
void
copyBufferToImage(VkCommandBuffer commandBuffer,
                  VkBuffer buffer,
                  VkImage image,
                  uint32_t width,
                  uint32_t height,
                  uint32_t depth,
                  uint32_t layerCount = 1);

} // namespace gfxvulkan
