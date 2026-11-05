#include "Backend.h"

#include "GestureRenderer.h"
#include "GLFramebufferObject.h"
#include "RenderGL.h"
#include "RenderGLPT.h"

namespace gfxopengl {

namespace {

GLenum
toGlInternalFormat(gfxApi::FramebufferColorFormat format)
{
  switch (format) {
    case gfxApi::FramebufferColorFormat::Rgba8:
      return GL_RGBA8;
    case gfxApi::FramebufferColorFormat::Rgba32F:
      return GL_RGBA32F;
  }
  return GL_RGBA8;
}

} // namespace

std::unique_ptr<gfxApi::IGestureRenderer>
Backend::createGestureRenderer()
{
  return std::make_unique<GestureRenderer>();
}

std::unique_ptr<gfxApi::IRenderWindow>
Backend::createRenderWindow(gfxApi::RenderWindowKind kind, RenderSettings* renderSettings)
{
  switch (kind) {
    case gfxApi::RenderWindowKind::RaymarchBlended:
      return std::make_unique<RenderGL>(renderSettings);
    case gfxApi::RenderWindowKind::PathTrace:
    default:
      return std::make_unique<RenderGLPT>(renderSettings);
  }
}

std::unique_ptr<gfxApi::Framebuffer>
Backend::createFramebuffer(const gfxApi::FramebufferDesc& desc)
{
  return std::make_unique<GLFramebufferObject>(
    desc.width, desc.height, toGlInternalFormat(desc.colorFormat), desc.depthStencil);
}

} // namespace gfxopengl
