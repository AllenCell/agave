#pragma once

#include "IFileReader.h"

#include <memory>
#include <string>
#include <vector>

class ImageXYZC;
struct VolumeDimensions;
struct MultiscaleDims;

class FileReader
{
public:
  FileReader();
  virtual ~FileReader();

  static IFileReader* getReader(const std::string& filepath, bool isImageSequence = false);

  // If `reader` is provided, it is reused to load the image (skipping the cost of
  // re-opening the file and re-parsing metadata). If null, a new reader is constructed
  // via getReader(). The result is stored in the CacheManager on success either way.
  static std::shared_ptr<ImageXYZC> loadAndCache(const LoadSpec& loadSpec,
                                                 std::shared_ptr<IFileReader> reader = nullptr);

  static std::shared_ptr<ImageXYZC> loadFromArray_4D(uint8_t* dataArray,
                                                     std::vector<uint32_t> shape,
                                                     const std::string& name,
                                                     std::vector<char> dims = {},
                                                     std::vector<std::string> channelNames = {},
                                                     std::vector<float> physicalSizes = { 1.0f, 1.0f, 1.0f },
                                                     std::string spatialUnits = "units",
                                                     bool addToCache = false);

private:
};
