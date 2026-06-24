#pragma once

#include "IGraphicsDevice.h"

#include <string>

namespace gfxApi {

// Parameters supplied to a backend at construction time.
struct InitParams
{
  // Filesystem path to renderer assets (shaders, etc.).
  std::string assetPath;
  // Run without an on-screen surface (offscreen / EGL rendering).
  bool headless = false;
  // Index of the GPU to use when more than one is available.
  int selectedGpu = 0;
  // Install a GL debug logger (verbose; for development).
  bool enableDebug = false;
};

// Abstract graphics backend. A backend owns the concrete IGraphicsDevice and
// any backend-global state. renderlib::initialize creates exactly one backend
// (currently always an OpenGL backend) and holds it for the process lifetime.
// All renderer code should reach GPU functionality through device() rather
// than touching backend-specific APIs.
class Backend
{
public:
  virtual ~Backend() = default;

  // The GPU device owned by this backend.
  virtual IGraphicsDevice& device() = 0;

  // The kind of backend this is.
  virtual BackendKind kind() const = 0;
};

} // namespace gfxApi
