#include "Backend.h"

#include "GestureRenderer.h"
#include "RenderGL.h"
#include "RenderGLPT.h"

namespace gfxopengl {

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

} // namespace gfxopengl
