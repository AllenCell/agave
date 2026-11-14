#include "UniformRing.h"

#include "Logging.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"

#include <algorithm>
#include <cstring>

namespace gfxvulkan::resources {

bool
UniformRing::ensure(Backend& backend,
                    VkDeviceSize slotSize,
                    uint32_t slots,
                    VkDescriptorSet descriptorSet,
                    uint32_t binding)
{
  if (slots <= m_slots) {
    return true;
  }

  VkDevice device = backend.logicalDevice();

  if (m_stride == 0) {
    // A dynamic uniform offset must be a multiple of the device's
    // minUniformBufferOffsetAlignment, so round the slot size up to that once.
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(backend.physicalDevice(), &properties);
    const VkDeviceSize align = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    m_stride = (slotSize + align - 1) / align * align;
  }

  // Grow geometrically so a frame that needs a few more slots than the last one
  // does not reallocate every time.
  const uint32_t newSlots = std::max<uint32_t>(slots, m_slots + m_slots / 2 + 1);
  reset(device);

  auto buffer =
    backend.device().createBuffer(m_stride * newSlots,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!buffer) {
    return false;
  }
  m_buffer = std::move(*buffer);
  if (vkMapMemory(device, m_buffer.memory(), 0, VK_WHOLE_SIZE, 0, &m_mapped) != VK_SUCCESS) {
    LOG_ERROR << "vkMapMemory for a uniform ring failed";
    m_mapped = nullptr;
    m_buffer.reset();
    return false;
  }
  m_slots = newSlots;

  if (descriptorSet != VK_NULL_HANDLE) {
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = m_buffer.get();
    bufferInfo.offset = 0;
    // One slot: the dynamic offset picks which one.
    bufferInfo.range = slotSize;
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }

  return true;
}

void
UniformRing::reset(VkDevice device)
{
  if (m_mapped) {
    vkUnmapMemory(device, m_buffer.memory());
    m_mapped = nullptr;
  }
  m_buffer.reset();
  m_slots = 0;
}

void
UniformRing::write(uint32_t slot, const void* data, size_t size) const
{
  std::memcpy(static_cast<uint8_t*>(m_mapped) + offsetForSlot(slot), data, size);
}

} // namespace gfxvulkan::resources
