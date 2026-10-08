#include "renderlib/ImageXYZC.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/io/FileReaderTestVolume.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace {
size_t
voxelIndex(uint32_t x, uint32_t y, uint32_t z)
{
  return (static_cast<size_t>(z) * FileReaderTestVolume::SIZE_Y + y) * FileReaderTestVolume::SIZE_X + x;
}
} // namespace

TEST_CASE("TESTVOLUME selects the procedural reader")
{
  CHECK(FileReaderTestVolume::PATH == "TESTVOLUME");
  std::unique_ptr<IFileReader> reader(FileReader::getReader(FileReaderTestVolume::PATH));
  REQUIRE(reader != nullptr);
  REQUIRE(dynamic_cast<FileReaderTestVolume*>(reader.get()) != nullptr);

  const VolumeDimensions dims = reader->loadDimensions(FileReaderTestVolume::PATH);
  CHECK(dims.sizeX == 256);
  CHECK(dims.sizeY == 256);
  CHECK(dims.sizeZ == 256);
  CHECK(dims.sizeC == 3);
  CHECK(dims.sizeT == 100);
  CHECK(dims.channelNames == std::vector<std::string>{ "Sphere", "Torus", "Cone" });
  CHECK(reader->loadNumScenes(FileReaderTestVolume::PATH) == 1);

  const auto multiscaleDims = reader->loadMultiscaleDims(FileReaderTestVolume::PATH);
  REQUIRE(multiscaleDims.size() == 1);
  CHECK(multiscaleDims[0].shape == std::vector<int64_t>{ 100, 3, 256, 256, 256 });
}

TEST_CASE("FileReaderTestVolume primitive utilities generate translated solids")
{
  std::vector<uint16_t> volume;

  FileReaderTestVolume::generateSphereVolume(volume, 10.0f, 0.0f, 0.0f);
  REQUIRE(volume.size() == static_cast<size_t>(256) * 256 * 256);
  CHECK(volume[voxelIndex(138, 128, 128)] == FileReaderTestVolume::FOREGROUND_VALUE);
  CHECK(volume[voxelIndex(30, 30, 30)] == 0);

  FileReaderTestVolume::generateTorusVolume(volume, 0.0f, 8.0f, 0.0f);
  CHECK(volume[voxelIndex(176, 136, 128)] == FileReaderTestVolume::FOREGROUND_VALUE);
  CHECK(volume[voxelIndex(128, 136, 128)] == 0);

  FileReaderTestVolume::generateConeVolume(volume, 0.0f, 0.0f, -6.0f);
  CHECK(volume[voxelIndex(128, 128, 72)] == FileReaderTestVolume::FOREGROUND_VALUE);
  CHECK(volume[voxelIndex(30, 30, 30)] == 0);
}
