#pragma once

#include "BlockingFileReader.h"
#include "VolumeDimensions.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ImageXYZC;

// Procedural time series used for exercising AGAVE without an input file.
// Select it by passing FileReaderTestVolume::PATH to FileReader::getReader().
class FileReaderTestVolume : public BlockingFileReader
{
public:
  static const std::string PATH;

  static constexpr uint32_t SIZE_X = 256;
  static constexpr uint32_t SIZE_Y = 256;
  static constexpr uint32_t SIZE_Z = 256;
  static constexpr uint32_t SIZE_C = 3;
  static constexpr uint32_t SIZE_T = 100;
  static constexpr uint16_t FOREGROUND_VALUE = 65535;

  explicit FileReaderTestVolume(const std::string& filepath);
  ~FileReaderTestVolume() override = default;

  bool supportChunkedLoading() const override { return false; }

  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override;
  VolumeDimensions loadDimensions(const std::string& filepath, uint32_t scene = 0) override;
  uint32_t loadNumScenes(const std::string& filepath) override;
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string& filepath, uint32_t scene = 0) override;

  // Fill a 256^3 uint16 volume with one translated solid primitive. The
  // destination is resized and cleared before the primitive is generated.
  static void generateSphereVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz);
  static void generateTorusVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz);
  static void generateConeVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz);

private:
  static size_t voxelIndex(uint32_t x, uint32_t y, uint32_t z);
};
