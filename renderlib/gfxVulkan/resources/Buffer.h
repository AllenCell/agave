#pragma once

#include "DeviceObject.h"

namespace gfxvulkan::resources {

// A buffer and its dedicated allocation are one ownership unit. The member
// order ensures the buffer is destroyed before its backing memory.
class Buffer
{
public:
  struct Released
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };

  Buffer() = default;
  Buffer(UniqueBuffer buffer, UniqueDeviceMemory memory, VkDeviceSize size)
    : m_memory(std::move(memory))
    , m_buffer(std::move(buffer))
    , m_size(size)
  {
  }

  Buffer(Buffer&&) noexcept = default;
  Buffer& operator=(Buffer&&) noexcept = default;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  VkBuffer get() const { return m_buffer.get(); }
  VkDeviceMemory memory() const { return m_memory.get(); }
  VkDeviceSize size() const { return m_size; }
  bool valid() const { return m_buffer && m_memory; }
  explicit operator bool() const { return valid(); }

  void reset()
  {
    m_buffer.reset();
    m_memory.reset();
    m_size = 0;
  }

  Released release()
  {
    m_size = 0;
    return { m_buffer.release(), m_memory.release() };
  }

private:
  UniqueDeviceMemory m_memory;
  UniqueBuffer m_buffer;
  VkDeviceSize m_size = 0;
};

} // namespace gfxvulkan::resources
