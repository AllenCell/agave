#pragma once

#include "DeviceObject.h"

namespace gfxvulkan::resources {

// An image and its dedicated allocation are one ownership unit. Image views
// remain separate because one image can legitimately have several views.
class Image
{
public:
  struct Released
  {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };

  Image() = default;
  Image(UniqueImage image, UniqueDeviceMemory memory)
    : m_memory(std::move(memory))
    , m_image(std::move(image))
  {
  }

  Image(Image&&) noexcept = default;
  Image& operator=(Image&&) noexcept = default;
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  VkImage get() const { return m_image.get(); }
  VkDeviceMemory memory() const { return m_memory.get(); }
  bool valid() const { return m_image && m_memory; }
  explicit operator bool() const { return valid(); }

  void reset()
  {
    m_image.reset();
    m_memory.reset();
  }

  Released release() { return { m_image.release(), m_memory.release() }; }

private:
  UniqueDeviceMemory m_memory;
  UniqueImage m_image;
};

} // namespace gfxvulkan::resources
