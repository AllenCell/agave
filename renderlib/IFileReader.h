#pragma once

#include "VolumeDimensions.h"

#include <memory>
#include <string>
#include <vector>

class ImageXYZC;
class LoadRequest;
struct MultiscaleDims;

// TODO this is sort of zarr specific
// and if we ever want to load multiscale CZI or TIFF
// then we probably need to generalize this differently
struct LoadSpec
{
  std::string filepath;
  bool isImageSequence;
  // important for zarr multiscale
  // (TODO should store multiscale index instead?  ...and then find subpath from metadata)
  std::string subpath;

  uint32_t scene;
  uint32_t time;
  // if empty, load all channels
  std::vector<uint32_t> channels;
  // set all to 0 to load all x,y,z
  uint32_t minx, maxx, miny, maxy, minz, maxz;

  LoadSpec()
    : scene(0)
    , time(0)
    , minx(0)
    , maxx(0)
    , miny(0)
    , maxy(0)
    , minz(0)
    , maxz(0)
    , isImageSequence(false)
  {
  }

  std::string toString() const;

  // GPU estimate for 4 channels and one time at 16bpp
  size_t getMemoryEstimate(int totalChannels) const;

  static std::string bytesToStringLabel(size_t mem, int decimals = 1);

  std::string getFilename() const;

  static std::string getFilename(const std::string& filepath);
};

class IFileReader
{
public:
  virtual ~IFileReader() = default;

  virtual double getPhysicalTime(int32_t t, const VolumeDimensions& dims) const
  {
    return static_cast<double>(t) * dims.timeUnit;
  }

  // return true if this reader can load sub-chunks in XYZ
  virtual bool supportChunkedLoading() const = 0;

  // find number of scenes
  virtual uint32_t loadNumScenes(const std::string& filepath) = 0;

  // return dimensions from scene in file
  virtual VolumeDimensions loadDimensions(const std::string& filepath, uint32_t scene = 0) = 0;

  // return dimensions from scene in file
  virtual std::vector<MultiscaleDims> loadMultiscaleDims(const std::string& filepath, uint32_t scene = 0) = 0;

  // Begin loading image data from file and return immediately. The returned
  // LoadRequest can be polled for completion, queried for progress, and
  // cancelled. Returns null if the load could not be started at all.
  //
  // Readers backed by a blocking library implement this by deriving from
  // BlockingFileReader rather than overriding it here.
  virtual std::shared_ptr<LoadRequest> submitLoad(const LoadSpec& loadSpec) = 0;

  // How many loads this reader can usefully have in flight at once. 1 means the
  // reader offers no concurrency; callers must not exceed this value.
  virtual uint32_t maxConcurrentLoads() const { return 1; }

  // Load image data from file, blocking until done. Convenience wrapper around
  // submitLoad; returns null on failure.
  std::shared_ptr<ImageXYZC> loadFromFile(const LoadSpec& loadSpec);
};
