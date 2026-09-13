#pragma once

#include "BlockingFileReader.h"
#include "VolumeDimensions.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CBoundingBox;
class ImageXYZC;

// Forward-declared so libCZI headers stay out of this header. The shared_ptr
// members are fine with an incomplete type because the destructor of this class
// is defined in the .cpp, where libCZI.h is included.
namespace libCZI {
class ICZIReader;
}

class FileReaderCzi : public BlockingFileReader
{
public:
  FileReaderCzi(const std::string& filepath);
  ~FileReaderCzi() override;

  bool supportChunkedLoading() const override { return false; }

  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override;
  VolumeDimensions loadDimensions(const std::string& filepath, uint32_t scene = 0) override;
  uint32_t loadNumScenes(const std::string& filepath) override;
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string& filepath, uint32_t scene = 0) override;

private:
  // Open `filepath` once and keep the reader. Opening a CZI parses the subblock
  // directory, and the dimension read on top of it parses the whole metadata
  // XML through pugixml -- both were previously repeated for every timepoint.
  // libCZI documents ICZIReader as safe to call from multiple threads
  // concurrently (libCZI.h), so one shared reader serves concurrent loads.
  std::shared_ptr<libCZI::ICZIReader> openReader(const std::string& filepath);

  // Memoized dimensions per scene, to avoid re-parsing the metadata XML.
  // Returns false on a failed read, mirroring readCziDimensions, so callers keep
  // their original failure criterion rather than inferring it from the value.
  bool cachedDimensions(const std::string& filepath, uint32_t scene, VolumeDimensions& dims);

  // Guards m_openPath, m_reader and m_dims. Only the lazy open and the memo
  // lookups are serialized; the actual plane reads run unlocked.
  std::mutex m_readerMutex;
  std::string m_openPath;
  std::shared_ptr<libCZI::ICZIReader> m_reader;
  std::map<uint32_t, VolumeDimensions> m_dims;
};
