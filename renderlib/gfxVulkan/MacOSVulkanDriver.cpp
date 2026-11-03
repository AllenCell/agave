#include "MacOSVulkanDriver.h"

#include <cstdlib>
#include <filesystem>

#include <dlfcn.h>

namespace gfxvulkan {

std::filesystem::path
FindMacOSVulkanManifest(const std::filesystem::path& moduleDir, const std::filesystem::path& buildManifest)
{
  const std::filesystem::path candidates[] = {
    moduleDir / "../Resources/vulkan/icd.d/libkosmickrisp_icd.json",
    moduleDir / "share/vulkan/icd.d/libkosmickrisp_icd.json",
    buildManifest,
  };
  std::error_code error;
  for (const auto& manifest : candidates) {
    if (std::filesystem::is_regular_file(manifest, error)) {
      return manifest.lexically_normal();
    }
  }
  return {};
}

bool
ConfigureMacOSVulkanDriver()
{
  if (std::getenv("VK_DRIVER_FILES") || std::getenv("VK_ICD_FILENAMES")) {
    return true;
  }

  // renderlib is static: this address belongs to the app executable or the
  // Python extension, so the same lookup works for either installed layout.
  static const char moduleAnchor = 0;
  Dl_info moduleInfo = {};
  if (!dladdr(&moduleAnchor, &moduleInfo) || !moduleInfo.dli_fname) {
    return false;
  }
  std::error_code error;
  const auto moduleDir = std::filesystem::absolute(moduleInfo.dli_fname, error).parent_path();
  if (error) {
    return false;
  }
  const auto manifest = FindMacOSVulkanManifest(moduleDir, AGAVE_KOSMICKRISP_BUILD_MANIFEST);
  return !manifest.empty() && setenv("VK_DRIVER_FILES", manifest.c_str(), 0) == 0;
}

} // namespace gfxvulkan
