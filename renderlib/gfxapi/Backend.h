#pragma once

#include "IGraphicsDevice.h"

namespace gfxApi {

// Process-wide accessor for the active graphics backend. The backend must be
// installed exactly once during renderlib initialization (see
// renderlib::initialize). All renderer code should go through device() to
// reach GPU functionality rather than touching backend-specific APIs.
class Backend
{
public:
  // Install the backend. Asserts if called more than once with a different
  // device, or if `device` is null.
  static void install(IGraphicsDevice* device);

  // Release the backend. The caller retains ownership of the device pointer
  // passed to install(); this only clears the registration.
  static void shutdown();

  // Returns the active device, or asserts if no backend has been installed.
  static IGraphicsDevice& device();

  // Returns true after install() has been called and before shutdown().
  static bool isInstalled();

  // Convenience accessor for the active backend kind.
  static BackendKind kind();
};

} // namespace gfxApi
