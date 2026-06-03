#include "Backend.h"

#include "Logging.h"

#include <cassert>

namespace gfxApi {

namespace {
IGraphicsDevice* g_device = nullptr;
}

void
Backend::install(IGraphicsDevice* device)
{
  assert(device != nullptr && "gfxApi::Backend::install requires a non-null device");
  if (g_device != nullptr && g_device != device) {
    LOG_ERROR << "gfxApi::Backend::install called twice with different devices";
    assert(false && "gfxApi backend already installed");
    return;
  }
  g_device = device;
}

void
Backend::shutdown()
{
  g_device = nullptr;
}

IGraphicsDevice&
Backend::device()
{
  assert(g_device != nullptr && "gfxApi backend not installed");
  return *g_device;
}

bool
Backend::isInstalled()
{
  return g_device != nullptr;
}

BackendKind
Backend::kind()
{
  return device().backend();
}

} // namespace gfxApi
