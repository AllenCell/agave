#pragma once

#include "Device.h"

#include "gfxapi/Backend.h"

namespace gfxopengl {

// OpenGL implementation of gfxApi::Backend. Owns the OpenGL graphics device.
//
// The GL context itself is still created by renderlib (via Qt / EGL); this
// backend supplies the gfxApi-facing device that renderer code talks to.
class Backend : public gfxApi::Backend
{
public:
  explicit Backend(const gfxApi::InitParams& params)
    : m_params(params)
  {
  }
  ~Backend() override = default;

  gfxApi::IGraphicsDevice& device() override { return m_device; }
  gfxApi::BackendKind kind() const override { return gfxApi::BackendKind::OpenGL; }

private:
  gfxApi::InitParams m_params;
  Device m_device;
};

} // namespace gfxopengl
