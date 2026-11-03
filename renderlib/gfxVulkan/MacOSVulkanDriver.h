#pragma once

#include <filesystem>

namespace gfxvulkan {

std::filesystem::path
FindMacOSVulkanManifest(const std::filesystem::path& moduleDir, const std::filesystem::path& buildManifest);

// Select the bundled KosmicKrisp ICD before the first Vulkan loader call.
// Explicit VK_DRIVER_FILES / VK_ICD_FILENAMES overrides take precedence.
bool
ConfigureMacOSVulkanDriver();

} // namespace gfxvulkan
