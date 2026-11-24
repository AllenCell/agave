#include "renderlib/ImageXYZC.h"
#include "renderlib/PythonRenderer.h"
#include "renderlib/VolumeDimensions.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/renderlib.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kSetResolution = 16;
constexpr int kRedraw = 15;

std::vector<uint8_t>
renderPixels(PythonRenderer& renderer)
{
  const auto frame = renderer.execute(kRedraw, {});
  const auto* image = std::get_if<PythonRendererImage>(&frame);
  REQUIRE(image != nullptr);
  return image->pixels;
}

// One uint16 channel whose intensity ramps along X, so X slices differ.
void
loadGradientVolume(PythonRenderer& renderer, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ)
{
  const size_t voxels = static_cast<size_t>(sizeX) * sizeY * sizeZ;
  auto data = std::make_unique<uint8_t[]>(voxels * sizeof(uint16_t));
  std::vector<uint16_t> values(voxels);
  for (size_t i = 0; i < voxels; ++i) {
    values[i] = static_cast<uint16_t>(1000 + 4000 * (i % sizeX));
  }
  std::memcpy(data.get(), values.data(), voxels * sizeof(uint16_t));

  VolumeDimensions dimensions;
  dimensions.sizeX = sizeX;
  dimensions.sizeY = sizeY;
  dimensions.sizeZ = sizeZ;
  dimensions.sizeC = 1;
  dimensions.sizeT = 1;
  dimensions.channelNames = { "ramp" };
  auto image =
    FileReader::loadFromArray_4D(std::move(data), { 1, sizeZ, sizeY, sizeX }, "ramp", { 'C', 'Z', 'Y', 'X' });
  renderer.loadVolume(image, dimensions, "ramp");
}
}

TEST_CASE("Python renderers share one Vulkan device", "[vulkan][python]")
{
  const std::string assets = "../renderlib/assets";
  std::unique_ptr<PythonRenderer> first;
  try {
    first = std::make_unique<PythonRenderer>("raymarch", assets, gfxApi::kAutoSelectGpu);
  } catch (const std::runtime_error&) {
    SKIP("No headless Vulkan device available");
  }

  gfxApi::Backend* backend = renderlib::graphicsBackend();
  REQUIRE(backend != nullptr);
  gfxApi::IGraphicsDevice* device = &backend->device();
  const int gpu = backend->selectedGpuIndex();
  REQUIRE(gpu >= 0);

  // An explicit request for the auto-selected GPU, and a second auto request,
  // both reuse the existing backend and device.
  PythonRenderer explicitSame("pathtrace", assets, gpu);
  PythonRenderer autoAgain("raymarch", assets, gfxApi::kAutoSelectGpu);
  REQUIRE(renderlib::graphicsBackend() == backend);
  REQUIRE(&renderlib::graphicsBackend()->device() == device);

  REQUIRE_THROWS_AS(PythonRenderer("raymarch", assets, gpu + 1), PythonRendererValueError);
  REQUIRE_THROWS_AS(PythonRenderer("raymarch", "other/assets", gpu), PythonRendererValueError);

  // The device survives until the last renderer closes.
  first->close();
  explicitSame.close();
  REQUIRE(renderlib::graphicsBackend() == backend);
  autoAgain.close();
  REQUIRE(renderlib::graphicsBackend() == nullptr);

  // Full shutdown releases the device; a new renderer brings it back up.
  PythonRenderer reopened("raymarch", assets, gpu);
  REQUIRE(renderlib::graphicsBackend() != nullptr);
  REQUIRE(renderlib::graphicsBackend()->selectedGpuIndex() == gpu);

  // Switching render modes keeps the renderer usable and on the shared device.
  reopened.execute(kSetResolution, { int32_t(64), int32_t(48) });
  for (const char* mode : { "pathtrace", "raymarch", "pathtrace" }) {
    reopened.setRenderMode(mode);
    const auto frame = reopened.execute(kRedraw, {});
    const auto* image = std::get_if<PythonRendererImage>(&frame);
    REQUIRE(image != nullptr);
    REQUIRE(image->width == 64);
    REQUIRE(image->height == 48);
  }
  REQUIRE_THROWS_AS(reopened.setRenderMode("bogus"), PythonRendererValueError);
  REQUIRE(renderlib::graphicsBackend()->selectedGpuIndex() == gpu);
}

TEST_CASE("Python renderers support every desktop render mode", "[vulkan][python]")
{
  const std::string assets = "../renderlib/assets";
  std::unique_ptr<PythonRenderer> renderer;
  try {
    renderer = std::make_unique<PythonRenderer>("triple", assets, gfxApi::kAutoSelectGpu);
  } catch (const std::runtime_error&) {
    SKIP("No headless Vulkan device available");
  }
  REQUIRE_THROWS_AS(PythonRenderer("bogus", assets, gfxApi::kAutoSelectGpu), PythonRendererValueError);

  renderer->execute(kSetResolution, { int32_t(64), int32_t(48) });
  loadGradientVolume(*renderer, 16, 12, 8);

  const auto isBlank = [](const std::vector<uint8_t>& pixels) {
    for (size_t i = 0; i < pixels.size(); i += 4) {
      if (pixels[i] || pixels[i + 1] || pixels[i + 2]) {
        return false;
      }
    }
    return true;
  };

  // The default intensity window darkens the low half of the ramp, so view the
  // brightest X plane. Every mode renders the volume, including slice <-> 3D swaps.
  renderer->setSliceIndices(15, 6, 4);
  for (const char* mode : { "triple", "slice_z", "slice_y", "slice_x", "raymarch", "pathtrace", "slice_z", "triple" }) {
    INFO(mode);
    renderer->setRenderMode(mode);
    const auto pixels = renderPixels(*renderer);
    REQUIRE(pixels.size() == 64u * 48u * 4u);
    REQUIRE_FALSE(isBlank(pixels));
  }

  // The X slice shows a single intensity of the ramp, so moving it changes the image.
  renderer->setRenderMode("slice_x");
  renderer->setSliceIndices(1, 6, 4);
  const auto lowX = renderPixels(*renderer);
  renderer->setSliceIndices(14, 6, 4);
  const auto highX = renderPixels(*renderer);
  REQUIRE(lowX != highX);
  // Out-of-range indices clamp to the last slice rather than failing.
  renderer->setSliceIndices(1000, 6, 4);
  const auto lastX = renderPixels(*renderer);
  renderer->setSliceIndices(15, 6, 4);
  REQUIRE(renderPixels(*renderer) == lastX);
}
