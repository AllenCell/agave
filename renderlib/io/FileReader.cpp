#include "FileReader.h"

#include "FileReaderCCP4.h"
#include "FileReaderCzi.h"
#include "FileReaderImageSequence.h"
#include "FileReaderTIFF.h"
#include "FileReaderTestVolume.h"
#include "FileReaderZarr.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "CacheManager.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>

// return file extension as lowercase
std::string
getExtension(const std::string filepath)
{
  std::filesystem::path fpath(filepath);

  std::filesystem::path ext = fpath.extension();
  std::string extstr = ext.string();
  for (std::string::size_type i = 0; i < extstr.length(); ++i) {
    extstr[i] = std::tolower(extstr[i]);
  }

  return extstr;
}

FileReader::FileReader() {}

FileReader::~FileReader() {}

IFileReader*
FileReader::getReader(const std::string& filepath, bool isImageSequence)
{
  if (filepath == FileReaderTestVolume::PATH) {
    return new FileReaderTestVolume(filepath);
  }

  std::string extstr = getExtension(filepath);

  if (isImageSequence && (extstr == ".tif" || extstr == ".tiff")) {
    return new FileReaderImageSequence(filepath);
  } else if (filepath.find("http") == 0) {
    return new FileReaderZarr(filepath);
  } else if (filepath.find("s3:") == 0) {
    return new FileReaderZarr(filepath);
  } else if (filepath.find("gs:") == 0) {
    return new FileReaderZarr(filepath);
  } else if (extstr == ".tif" || extstr == ".tiff") {
    return new FileReaderTIFF(filepath);
  } else if (extstr == ".czi") {
    return new FileReaderCzi(filepath);
  } else if (extstr == ".map" || extstr == ".mrc") {
    return new FileReaderCCP4(filepath);
  } else if (extstr == ".zarr") {
    return new FileReaderZarr(filepath);
  }
  // if it's a directory, and contains the string zarr anywhere, we assume it's a zarr
  else if (std::filesystem::is_directory(filepath)) {
    if (filepath.find("zarr") != std::string::npos) {
      return new FileReaderZarr(filepath);
    }
  }

  return nullptr;
}

std::shared_ptr<ImageXYZC>
FileReader::loadAndCache(const LoadSpec& loadSpec, std::shared_ptr<IFileReader> reader)
{
  auto cached = CacheManager::instance().findImage(loadSpec);
  if (cached) {
    return cached;
  }

  std::shared_ptr<ImageXYZC> image;

  const std::string& filepath = loadSpec.filepath;

  // Fall back to constructing a new reader if the caller didn't supply one.
  // A reused reader can skip re-opening the file and re-parsing metadata (notably
  // valuable for time-series stepping on OME-Zarr/TIFF/CZI).
  std::shared_ptr<IFileReader> ownedReader;
  if (!reader) {
    ownedReader.reset(FileReader::getReader(filepath, loadSpec.isImageSequence));
    if (!ownedReader) {
      LOG_ERROR << "Could not find a reader for file " << filepath;
      return nullptr;
    }
    reader = ownedReader;
  }

  image = reader->loadFromFile(loadSpec);

  if (image) {
    CacheManager::instance().storeImage(loadSpec, image);
  }

  return image;
}

std::shared_ptr<ImageXYZC>
FileReader::loadFromArray_4D(std::unique_ptr<uint8_t[]> dataArray,
                             std::vector<uint32_t> shape,
                             const std::string& name,
                             std::vector<char> dims,
                             std::vector<std::string> channelNames,
                             std::vector<float> physicalSizes,
                             std::string spatialUnits,
                             bool addToCache)
{
  LoadSpec cacheSpec;
  cacheSpec.filepath = name;
  if (addToCache) {
    auto cached = CacheManager::instance().findImage(cacheSpec);
    if (cached) {
      return cached;
    }
  }

  // assume data is in CZYX order:
  static const int XDIM = 3, YDIM = 2, ZDIM = 1, CDIM = 0;

  if (!dataArray) {
    throw std::invalid_argument("Array data must not be null");
  }
  if (shape.size() != 4 || (!dims.empty() && dims != std::vector<char>{ 'C', 'Z', 'Y', 'X' })) {
    throw std::invalid_argument("Array data must use CZYX dimension order");
  }
  for (uint32_t extent : shape) {
    if (extent == 0) {
      throw std::invalid_argument("Array dimensions must be nonzero");
    }
  }

  uint32_t bpp = 16;
  uint32_t sizeX = shape[XDIM];
  uint32_t sizeY = shape[YDIM];
  uint32_t sizeZ = shape[ZDIM];
  uint32_t sizeC = shape[CDIM];
  if (physicalSizes.size() != 3 || physicalSizes[0] <= 0.0f || physicalSizes[1] <= 0.0f ||
      physicalSizes[2] <= 0.0f) {
    throw std::invalid_argument("Physical voxel sizes must contain three positive values");
  }
  float physicalSizeX = physicalSizes[0];
  float physicalSizeY = physicalSizes[1];
  float physicalSizeZ = physicalSizes[2];

  if (channelNames.empty()) {
    channelNames.reserve(sizeC);
    for (uint32_t channel = 0; channel < sizeC; ++channel) {
      channelNames.push_back("Channel " + std::to_string(channel));
    }
  } else if (channelNames.size() != sizeC) {
    throw std::invalid_argument("Channel name count must match the array channel count");
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  // Keep ownership until construction succeeds, then transfer it to ImageXYZC.
  ImageXYZC* im = new ImageXYZC(sizeX,
                                sizeY,
                                sizeZ,
                                sizeC,
                                uint32_t(bpp),
                                dataArray.get(),
                                physicalSizeX,
                                physicalSizeY,
                                physicalSizeZ,
                                spatialUnits);
  dataArray.release();

  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = endTime - startTime;
  LOG_DEBUG << "ImageXYZC prepared in " << (elapsed.count() * 1000.0) << "ms";

  im->setChannelNames(channelNames);

  std::shared_ptr<ImageXYZC> sharedImage(im);
  if (addToCache) {
    LoadSpec newSpec;
    newSpec.filepath = name;
    CacheManager::instance().storeImage(newSpec, sharedImage);
  }
  return sharedImage;
}

size_t
LoadSpec::getMemoryEstimate(int totalChannels) const
{
  size_t npix = 1;
  npix *= (maxx - minx);
  npix *= (maxy - miny);
  npix *= (maxz - minz);
  // on gpu we upload only 4 channels max
  int nch = 4;
  if (channels.empty()) {
    // then get the number of channels from the image
    nch = std::min(nch, totalChannels);
  } else if (!channels.empty() && channels.size() < 4) {
    nch = channels.size();
  }
  size_t bytesperpixel = nch * ImageXYZC::IN_MEMORY_BPP / 8; // 4 channels * 2 bytes per channel
  size_t mem = npix * bytesperpixel;                         // overflow?
  return mem;
}

std::string
LoadSpec::getFilename(const std::string& filepath)
{
  if (filepath.empty()) {
    return filepath;
  }
  std::string filename = filepath.substr(filepath.rfind("/") + 1);
  if (filename.empty()) {
    // try the next slash
    filename = filepath;
    filename.pop_back();
    filename = filename.substr(filename.rfind("/") + 1);
  }
  return filename;
}

std::string
LoadSpec::getFilename() const
{
  return LoadSpec::getFilename(filepath);
}

std::string
LoadSpec::bytesToStringLabel(size_t mem, int decimals)
{
  static const std::vector<std::string> levels = { "B", "KB", "MB", "GB", "TB", "PB" };
  double memvalue = mem;
  int level = 0;
  while (memvalue > 1024.0 && level < levels.size() - 1) {
    memvalue = memvalue / 1024.0;
    level++;
  }

  std::stringstream stream;
  stream << std::fixed << std::setprecision(decimals) << memvalue;
  stream << " " << levels[level];
  std::string s = stream.str();

  return s;
}

std::string
LoadSpec::toString() const
{
  std::stringstream stream;
  stream << filepath;
  if (!subpath.empty()) {
    stream << " " << subpath;
  }
  if (isImageSequence) {
    stream << " (sequence)";
  }
  stream << " : scene " << scene << " time " << time;
  stream << " : channels [";
  for (auto i : channels) {
    stream << i << ",";
  }
  stream << "]";
  stream << " X:[" << minx << "," << maxx << "] Y[" << miny << "," << maxy << "] Z[" << minz << "," << maxz << "]";
  std::string s = stream.str();
  return s;
}
