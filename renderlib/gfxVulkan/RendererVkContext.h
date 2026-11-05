#pragma once

#include "gfxapi/IGLContext.h"

namespace gfxvulkan {

class Backend;

class RendererVkContext : public gfxApi::IGLContext
{
public:
  explicit RendererVkContext(Backend& backend);

  bool create() override;
  bool isValid() const override;
  bool makeCurrent() override;
  void doneCurrent() override;

private:
  Backend& m_backend;
};

} // namespace gfxvulkan
