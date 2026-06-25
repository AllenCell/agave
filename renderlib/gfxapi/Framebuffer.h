#pragma once

#include <cstdint>

namespace gfxApi {

enum class FramebufferColorFormat : uint8_t
{
  Rgba8,
  Rgba32F,
};

struct FramebufferDesc
{
  uint32_t width = 0;
  uint32_t height = 0;
  FramebufferColorFormat colorFormat = FramebufferColorFormat::Rgba8;
  bool depthStencil = false;
};

class Framebuffer
{
public:
  virtual ~Framebuffer() = default;

  virtual void bind() = 0;
  virtual void release() = 0;
  virtual void resize(uint32_t width, uint32_t height) = 0;

  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;

  // pixels must be preallocated for 32 bits per pixel.
  virtual void toImage(void* pixels) = 0;
};

} // namespace gfxApi
