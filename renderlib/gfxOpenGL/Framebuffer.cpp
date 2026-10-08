#include "Framebuffer.h"

#include "Util.h"

Framebuffer::Framebuffer(uint32_t w, uint32_t h, GLenum colorFormat, bool depthStencil)
  : m_id(0)
  , m_colorTextureId(0)
  , m_depthTextureId(0)
  , m_w(0)
  , m_h(0)
  , m_colorFormat(colorFormat)
  , m_hasDepth(depthStencil)

{
  resize(w, h);
}

Framebuffer::~Framebuffer()
{
  destroyFb();
}

void
Framebuffer::resize(uint32_t w, uint32_t h)
{
  if (w == m_w && h == m_h) {
    return;
  }

  // blow away the entire FB instead of trying to preserve the FBO
  destroyFb();

  glGenTextures(1, &m_colorTextureId);
  check_gl("Gen fb texture id");
  glBindTexture(GL_TEXTURE_2D, m_colorTextureId);
  check_gl("Bind fb texture");
  // glTextureStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  glTexImage2D(GL_TEXTURE_2D, 0, m_colorFormat, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  check_gl("Create fb texture");
  // this is required in order to "complete" the texture object for mipmapless shader access.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  // unbind the texture
  glBindTexture(GL_TEXTURE_2D, 0);

  if (m_hasDepth) {
    // create depth texture
    glGenTextures(1, &m_depthTextureId);
    check_gl("Gen fb depth texture id");
    glBindTexture(GL_TEXTURE_2D, m_depthTextureId);
    check_gl("Bind fb depth texture");
    // glTextureStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    check_gl("Create fb depth texture");
    // this is required in order to "complete" the texture object for mipmapless shader access.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    // unbind the texture
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  glGenFramebuffers(1, &m_id);
  glBindFramebuffer(GL_FRAMEBUFFER, m_id);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_colorTextureId, 0);
  if (m_hasDepth) {
    // attach depth texture
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_depthTextureId, 0);
  }
  check_glfb("resized fb");
}

void
Framebuffer::destroyFb()
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  check_gl("bind 0 framebuffer");

  glBindTexture(GL_TEXTURE_2D, 0);
  check_gl("bind 0 texture");
  if (glIsTexture(m_colorTextureId)) {
    glDeleteTextures(1, &m_colorTextureId);
    check_gl("Destroy fb texture");
  }
  m_colorTextureId = 0;

  if (m_hasDepth) {
    if (glIsTexture(m_depthTextureId)) {
      glDeleteTextures(1, &m_depthTextureId);
      check_gl("Destroy fb depth texture");
    }
    m_depthTextureId = 0;
  }

  if (glIsFramebuffer(m_id)) {
    glDeleteFramebuffers(1, &m_id);
    check_gl("delete framebuffer id");
  }
  m_id = 0;
}
