#include "FileReader.h"

#include "FileReaderCCP4.h"
#include "FileReaderCzi.h"
#include "FileReaderImageSequence.h"
#include "FileReaderTIFF.h"
#include "FileReaderZarr.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "CacheManager.h"

#include <chrono>
#include <filesystem>

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
FileReader::loadFromArray_4D(uint8_t* dataArray,
                             std::vector<uint32_t> shape,
                             const std::string& name,
                             std::vector<char> dims,
                             std::vector<std::string> channelNames,
                             std::vector<float> physicalSizes,
                             std::string spatialUnits,
                             bool addToCache)
{
  // check cache first of all.
  LoadSpec cacheSpec;
  cacheSpec.filepath = name;
  auto cached = CacheManager::instance().findImage(cacheSpec);
  if (cached) {
    return cached;
  }

  // assume data is in CZYX order:
  static const int XDIM = 3, YDIM = 2, ZDIM = 1, CDIM = 0;

  size_t ndim = shape.size();
  assert(ndim == 4);

  uint32_t bpp = 16;
  uint32_t sizeT = 1;
  uint32_t sizeX = shape[XDIM];
  uint32_t sizeY = shape[YDIM];
  uint32_t sizeZ = shape[ZDIM];
  uint32_t sizeC = shape[CDIM];
  assert(physicalSizes.size() == 3);
  float physicalSizeX = physicalSizes[0];
  float physicalSizeY = physicalSizes[1];
  float physicalSizeZ = physicalSizes[2];

  // product of all shape elements must equal number of elements in dataArray
  // dims must either be empty or must be of same length as shape, and end in (Y, X), and start with CZ or ZC or Z ?

  auto startTime = std::chrono::high_resolution_clock::now();

  // note that im will take ownership of dataArray
  ImageXYZC* im = new ImageXYZC(
    sizeX, sizeY, sizeZ, sizeC, uint32_t(bpp), dataArray, physicalSizeX, physicalSizeY, physicalSizeZ, spatialUnits);

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
