#include "renderlib/ImageXYZC.h"
#include "renderlib/VolumeDimensions.h"
#include "renderlib/io/ConvertChannelData.h"
#include "renderlib/io/FileReader.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

TEST_CASE("Array volume pixels convert to ImageXYZC's uint16 format", "[array-volume]")
{
  VolumeDimensions dims;
  dims.sizeX = 3;
  dims.sizeY = 1;
  dims.sizeZ = 1;

  SECTION("uint8 values are widened without rescaling")
  {
    const uint8_t source[] = { 0, 127, 255 };
    uint16_t destination[3] = {};
    dims.bitsPerPixel = 8;

    REQUIRE(FileReaderUtil::convertChannelData(reinterpret_cast<uint8_t*>(destination), source, dims));
    REQUIRE(destination[0] == 0);
    REQUIRE(destination[1] == 127);
    REQUIRE(destination[2] == 255);
  }

  SECTION("float32 values are normalized across one channel")
  {
    const float source[] = { -1.0f, 0.0f, 1.0f };
    uint16_t destination[3] = {};
    dims.bitsPerPixel = 32;

    REQUIRE(FileReaderUtil::convertChannelData(
      reinterpret_cast<uint8_t*>(destination), reinterpret_cast<const uint8_t*>(source), dims));
    REQUIRE(destination[0] == 0);
    REQUIRE(destination[1] == 32767);
    REQUIRE(destination[2] == 65535);
  }

  SECTION("constant float32 channels map to zero")
  {
    const float source[] = { 4.0f, 4.0f, 4.0f };
    uint16_t destination[3] = { 1, 1, 1 };
    dims.bitsPerPixel = 32;

    REQUIRE(FileReaderUtil::convertChannelData(
      reinterpret_cast<uint8_t*>(destination), reinterpret_cast<const uint8_t*>(source), dims));
    REQUIRE(destination[0] == 0);
    REQUIRE(destination[1] == 0);
    REQUIRE(destination[2] == 0);
  }

  SECTION("non-finite float32 values are rejected during the existing min-max pass")
  {
    const float source[] = { 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f };
    uint16_t destination[3] = {};
    dims.bitsPerPixel = 32;

    REQUIRE_FALSE(FileReaderUtil::convertChannelData(
      reinterpret_cast<uint8_t*>(destination), reinterpret_cast<const uint8_t*>(source), dims));
  }
}

TEST_CASE("In-memory CZYX data constructs an ImageXYZC", "[array-volume]")
{
  auto pixels = std::make_unique<uint8_t[]>(8 * sizeof(uint16_t));
  auto* pixels16 = reinterpret_cast<uint16_t*>(pixels.get());
  for (uint16_t value = 0; value < 8; ++value) {
    pixels16[value] = value;
  }

  auto image = FileReader::loadFromArray_4D(std::move(pixels),
                                             { 1, 2, 2, 2 },
                                             "test-array",
                                             { 'C', 'Z', 'Y', 'X' },
                                             {},
                                             { 0.5f, 0.6f, 0.7f },
                                             "um",
                                             false);

  REQUIRE(image->sizeX() == 2);
  REQUIRE(image->sizeY() == 2);
  REQUIRE(image->sizeZ() == 2);
  REQUIRE(image->sizeC() == 1);
  REQUIRE(image->physicalSizeX() == 0.5f);
  REQUIRE(image->physicalSizeY() == 0.6f);
  REQUIRE(image->physicalSizeZ() == 0.7f);
  REQUIRE(image->spatialUnits() == "um");
  REQUIRE(image->channel(0)->m_name == "Channel 0");
  REQUIRE(reinterpret_cast<uint16_t*>(image->ptr())[7] == 7);
}
