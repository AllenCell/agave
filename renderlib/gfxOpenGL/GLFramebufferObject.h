#pragma once

#include "gfxapi/Framebuffer.h"
#include "glad/include/glad/glad.h"

#include <cstdint>

// RAII; must have a current GL context at creation and destruction time.
class GLFramebufferObject : public gfxApi::Framebuffer
{
public:
  GLFramebufferObject(uint32_t width, uint32_t height, GLenum colorInternalFormat = GL_RGBA8, bool depthStencil = false);
  ~GLFramebufferObject() override;

  void bind() override;
  void release() override;
  void resize(uint32_t width, uint32_t height) override;

  uint32_t width() const override { return m_width; }
  uint32_t height() const override { return m_height; }

  void toImage(void* pixels) override;

  GLuint id() const { return m_id; }
  GLuint colorTextureId() const { return m_colorTextureId; }

private:
  void destroy();
  void internalResize(uint32_t width, uint32_t height);

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  GLenum m_colorInternalFormat = GL_RGBA8;
  bool m_hasDepthStencil = false;

  GLuint m_id = 0;
  GLuint m_colorTextureId = 0;
  GLuint m_depthStencilRenderbufferId = 0;
};
