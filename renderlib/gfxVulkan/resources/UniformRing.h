#pragma once

#include "Buffer.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace gfxvulkan {

class Backend;

namespace resources {

// A host-visible ring of equally sized uniform slots, bound as a dynamic
// uniform descriptor (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) and addressed
// with a per-draw dynamic offset.
//
// Reach for this when a frame issues several draws that each need their own
// small block of uniform data, and you want them to share one command buffer.
// The cases that motivated it:
//
//   * The gesture overlay draws each command sequence with a different
//     projection matrix -- the 3d sequences use the camera's view-projection
//     matrix, the 2d screen sequence uses a screen-space ortho matrix -- so the
//     matrix has to change between draws within a single frame.
//   * Each thick-line strip is drawn with its own index into the shared
//     strip-vertex buffer and its own line thickness, so drawing the twelve
//     edges of a bounding box needs twelve different uniform blocks.
//   * The path tracer records several accumulation iterations into one frame,
//     and each iteration needs its own sample index to seed the shader's random
//     sequence. (RenderVkPT currently hand-rolls its own ring for this; it is
//     the obvious next user of this type.)
//
// The alternative, a single-slot uniform buffer rewritten between draws, forces
// the draws apart: the CPU has to wait for the GPU to finish reading slot N
// before it can write slot N+1, which means one submission and one fence wait
// per draw. Those stalls dominate when the draws themselves are cheap. With a
// ring, the CPU writes every slot up front, records all the draws with a
// different dynamic offset each, and submits once.
//
// Use it when:
//   * the per-draw data is uniform-buffer shaped (a matrix or a handful of
//     vectors) and the draw count varies at runtime;
//   * the draws are recorded into one command buffer, or into buffers whose
//     submissions do not overlap.
// Prefer something else when:
//   * one small struct is enough for the whole submission -- a plain uniform
//     buffer is simpler;
//   * the data is tiny and the shader can take push constants instead, which
//     avoids the descriptor entirely (but needs shader changes);
//   * submissions overlap in flight -- this ring has no per-frame fencing, so
//     the caller must not overwrite a slot the GPU may still be reading.
//
// Requirements: the descriptor set layout binding must be declared
// UNIFORM_BUFFER_DYNAMIC, and vkCmdBindDescriptorSets must be given exactly one
// dynamic offset (from offsetForSlot) for it.
//
// The allocation stays mapped for its lifetime, so writing a slot is a plain
// memcpy. Host-coherent memory means no explicit flush is needed; writes issued
// before the command buffer is submitted are visible to the GPU.
class UniformRing
{
public:
  UniformRing() = default;
  UniformRing(UniformRing&&) noexcept = default;
  UniformRing& operator=(UniformRing&&) noexcept = default;
  UniformRing(const UniformRing&) = delete;
  UniformRing& operator=(const UniformRing&) = delete;

  // Make room for at least `slots` slots of `slotSize` bytes, rounding the
  // stride up to the device's minimum dynamic-offset alignment. Growth is
  // geometric and the ring never shrinks. Whenever the buffer is (re)allocated,
  // `binding` of `descriptorSet` is repointed at it; pass VK_NULL_HANDLE when
  // the descriptor set does not exist yet (the caller then writes it itself).
  // Returns false only if the buffer could not be allocated or mapped.
  bool ensure(Backend& backend,
              VkDeviceSize slotSize,
              uint32_t slots,
              VkDescriptorSet descriptorSet,
              uint32_t binding = 0);

  // Unmaps and releases the allocation. Safe to call on an empty ring.
  void reset(VkDevice device);

  VkBuffer buffer() const { return m_buffer.get(); }
  uint32_t slots() const { return m_slots; }
  bool valid() const { return m_mapped != nullptr; }
  explicit operator bool() const { return valid(); }

  // Byte offset of a slot, to pass as a dynamic offset to
  // vkCmdBindDescriptorSets.
  uint32_t offsetForSlot(uint32_t slot) const { return static_cast<uint32_t>(slot * m_stride); }

  // Copy one slot's worth of uniform data into the ring. The caller is
  // responsible for keeping `slot` below slots().
  void write(uint32_t slot, const void* data, size_t size) const;

private:
  Buffer m_buffer;
  void* m_mapped = nullptr;
  uint32_t m_slots = 0;
  VkDeviceSize m_stride = 0;
};

} // namespace resources
} // namespace gfxvulkan
