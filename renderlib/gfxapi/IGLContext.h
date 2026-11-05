#pragma once

namespace gfxApi {

// Minimal interface for making an OpenGL context current without exposing the
// windowing toolkit that owns it.
class IGLContext
{
public:
  virtual ~IGLContext() = default;

  virtual bool create() = 0;
  virtual bool isValid() const = 0;
  virtual bool makeCurrent() = 0;
  virtual void doneCurrent() = 0;
};

} // namespace gfxApi
