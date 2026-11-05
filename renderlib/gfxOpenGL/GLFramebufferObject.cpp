#include "GLFramebufferObject.h"

#include "gfxOpenGL/Util.h"

GLFramebufferObject::GLFramebufferObject(uint32_t width, uint32_t height, GLenum colorInternalFormat, bool depthStencil)
  : m_colorInternalFormat(colorInternalFormat)
  , m_hasDepthStencil(depthStencil)
{
  internalResize(width, height);
}

GLFramebufferObject::~GLFramebufferObject()
{
  destroy();
}

void
GLFramebufferObject::resize(uint32_t width, uint32_t height)
{
  internalResize(width, height);
}

void
GLFramebufferObject::internalResize(uint32_t width, uint32_t height)
{
  if (width == m_width && height == m_height) {
    return;
  }

  destroy();

  glGenTextures(1, &m_colorTextureId);
  check_gl("Gen fb texture id");
  glBindTexture(GL_TEXTURE_2D, m_colorTextureId);
  check_gl("Bind fb texture");

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, m_colorInternalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  check_gl("Create fb texture");
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenFramebuffers(1, &m_id);
  glBindFramebuffer(GL_FRAMEBUFFER, m_id);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTextureId, 0);

  if (m_hasDepthStencil) {
    glGenRenderbuffers(1, &m_depthStencilRenderbufferId);
    check_gl("Gen fb depth stencil renderbuffer id");
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRenderbufferId);
    check_gl("Bind fb depth stencil renderbuffer");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    check_gl("Create fb depth stencil renderbuffer");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthStencilRenderbufferId);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthStencilRenderbufferId);
  }

  bool valid = check_glfb("GLFramebufferObject creation");
  if (!valid) {
    destroy();
    return;
  }

  m_width = width;
  m_height = height;
}

void
GLFramebufferObject::destroy()
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

  if (glIsRenderbuffer(m_depthStencilRenderbufferId)) {
    glDeleteRenderbuffers(1, &m_depthStencilRenderbufferId);
    check_gl("Destroy fb depth stencil renderbuffer");
  }
  m_depthStencilRenderbufferId = 0;

  if (glIsFramebuffer(m_id)) {
    glDeleteFramebuffers(1, &m_id);
    check_gl("delete framebuffer id");
  }
  m_id = 0;
  m_width = 0;
  m_height = 0;
}

void
GLFramebufferObject::bind()
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_id);
}

void
GLFramebufferObject::release()
{
  // TODO what about headless offscreen? There is no default surface.
  // glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
GLFramebufferObject::toImage(void* pixels)
{
  GLuint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&prevFbo);

  if (prevFbo != m_id) {
    bind();
  }

  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, width(), height(), GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);

  glReadBuffer(GL_COLOR_ATTACHMENT0);
  if (prevFbo != m_id) {
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
  }
}
