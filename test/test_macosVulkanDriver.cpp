#include "renderlib/gfxVulkan/MacOSVulkanDriver.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

struct ManifestFixture
{
  std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("agave-vulkan-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  ~ManifestFixture()
  {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }

  static void Write(const std::filesystem::path& path)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "{}";
  }
};

} // namespace

TEST_CASE("macOS Vulkan respects explicit driver overrides", "[vulkan]")
{
  struct EnvironmentGuard
  {
    std::optional<std::string> driverFiles;
    std::optional<std::string> icdFilenames;

    EnvironmentGuard()
    {
      if (const auto* value = std::getenv("VK_DRIVER_FILES")) {
        driverFiles = value;
      }
      if (const auto* value = std::getenv("VK_ICD_FILENAMES")) {
        icdFilenames = value;
      }
      unsetenv("VK_DRIVER_FILES");
      unsetenv("VK_ICD_FILENAMES");
    }

    ~EnvironmentGuard()
    {
      unsetenv("VK_DRIVER_FILES");
      unsetenv("VK_ICD_FILENAMES");
      if (driverFiles) {
        setenv("VK_DRIVER_FILES", driverFiles->c_str(), 1);
      }
      if (icdFilenames) {
        setenv("VK_ICD_FILENAMES", icdFilenames->c_str(), 1);
      }
    }
  } guard;

  const auto* variable = GENERATE("VK_DRIVER_FILES", "VK_ICD_FILENAMES");
  REQUIRE((setenv(variable, "/custom/driver.json", 1) == 0));
  REQUIRE(gfxvulkan::ConfigureMacOSVulkanDriver());
  const auto* selected = std::getenv(variable);
  REQUIRE((selected && std::string(selected) == "/custom/driver.json"));
  if (std::string(variable) == "VK_ICD_FILENAMES") {
    REQUIRE((std::getenv("VK_DRIVER_FILES") == nullptr));
  }
}

TEST_CASE("macOS Vulkan manifest discovery", "[vulkan]")
{
  ManifestFixture fixture;
  const auto sdkManifest = fixture.root / "sdk/libkosmickrisp_icd.json";
  const auto moduleDir = fixture.root / "agave.app/Contents/MacOS";
  std::filesystem::create_directories(moduleDir);

  REQUIRE(gfxvulkan::FindMacOSVulkanManifest(moduleDir, sdkManifest).empty());

  ManifestFixture::Write(sdkManifest);
  REQUIRE((gfxvulkan::FindMacOSVulkanManifest(moduleDir, sdkManifest) == sdkManifest));

  SECTION("the app bundle takes precedence over the build SDK")
  {
    const auto bundled = fixture.root / "agave.app/Contents/Resources/vulkan/icd.d/libkosmickrisp_icd.json";
    ManifestFixture::Write(bundled);
    REQUIRE((gfxvulkan::FindMacOSVulkanManifest(moduleDir, sdkManifest) == bundled));
    std::filesystem::remove(sdkManifest);
    REQUIRE((gfxvulkan::FindMacOSVulkanManifest(moduleDir, sdkManifest) == bundled));
  }

  SECTION("a Python extension uses its package, independently of the host executable")
  {
    const auto packageDir = fixture.root / "site-packages/agave_pyvk";
    const auto bundled = packageDir / "share/vulkan/icd.d/libkosmickrisp_icd.json";
    ManifestFixture::Write(bundled);
    REQUIRE((gfxvulkan::FindMacOSVulkanManifest(packageDir, sdkManifest) == bundled));
    std::filesystem::remove(sdkManifest);
    REQUIRE((gfxvulkan::FindMacOSVulkanManifest(packageDir, sdkManifest) == bundled));
  }
}
