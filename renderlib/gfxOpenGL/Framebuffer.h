#pragma once

#include "glad/glad.h"

#include <inttypes.h>

class Framebuffer
{
public:
  Framebuffer(uint32_t w, uint32_t h, GLenum colorFormat = GL_RGBA8, bool depthstencil = false);
  virtual ~Framebuffer();

  void resize(uint32_t w, uint32_t h);

  GLuint id() const { return m_id; }
  GLuint colorTextureId() const { return m_colorTextureId; }

private:
  void destroyFb();

  uint32_t m_w, m_h;
  GLenum m_colorFormat;
  bool m_hasDepth;
  GLuint m_id;
  GLuint m_colorTextureId;
  GLuint m_depthTextureId;
};
