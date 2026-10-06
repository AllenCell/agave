#include "Backend.h"

#include "GestureRenderer.h"

namespace gfxopengl {

std::unique_ptr<gfxApi::IGestureRenderer>
Backend::createGestureRenderer()
{
  return std::make_unique<GestureRenderer>();
}

} // namespace gfxopengl
