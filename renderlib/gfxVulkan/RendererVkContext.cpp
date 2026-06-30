#include "RendererVkContext.h"

#include "Backend.h"

namespace gfxvulkan {

RendererVkContext::RendererVkContext(Backend& backend)
  : m_backend(backend)
{
}

bool
RendererVkContext::create()
{
  return isValid();
}

bool
RendererVkContext::isValid() const
{
  return m_backend.isValid();
}

bool
RendererVkContext::makeCurrent()
{
  return isValid();
}

void
RendererVkContext::doneCurrent()
{
}

} // namespace gfxvulkan
