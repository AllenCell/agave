#pragma once

#include "BlockingFileReader.h"
#include "VolumeDimensions.h"

#include "tensorstore/context.h"
#include "tensorstore/tensorstore.h"

// must include after tensorstore so that tensorstore picks up its own internal json impl
#include "json/json.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>

class ImageXYZC;

class FileReaderZarr : public BlockingFileReader
{
public:
  FileReaderZarr(const std::string& filepath);
  ~FileReaderZarr() override;

  bool supportChunkedLoading() const override { return true; }

  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override;
  VolumeDimensions loadDimensions(const std::string& filepath, uint32_t scene = 0) override;
  uint32_t loadNumScenes(const std::string& filepath) override;
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string& filepath, uint32_t scene = 0) override;

private:
  nlohmann::json jsonRead(const std::string& filepath);
  std::vector<std::string> getChannelNames(const std::string& filepath);

  nlohmann::json getMultiscales(nlohmann::json attrs);
  nlohmann::json getOmero(nlohmann::json attrs);
  std::string tensorstoreZarrDriverName();

  // Does the actual metadata parse, including a tensorstore::Open per multiscale
  // level to read its shape. loadMultiscaleDims memoizes the result of this.
  std::vector<MultiscaleDims> readMultiscaleDims(const std::string& filepath, uint32_t scene);

  // Guards m_zarrVersion, m_zattrs and m_multiscaleDims. Recursive because
  // loadMultiscaleDims and getChannelNames both call jsonRead, which also locks.
  std::recursive_mutex m_metadataMutex;
  int m_zarrVersion;
  nlohmann::json m_zattrs;
  // Memoized readMultiscaleDims results, keyed by "<filepath>|<scene>". The
  // metadata is treated as fixed for the lifetime of the reader, matching the
  // RecheckCached{false} already used when opening m_store: a time series is
  // read many times and re-parsing per timepoint was pure overhead.
  std::map<std::string, std::vector<MultiscaleDims>> m_multiscaleDims;

  // Guards the lazy m_store open. Separate from m_metadataMutex so a load does
  // not hold the metadata lock across a store open. Never take m_metadataMutex
  // while holding this one.
  std::mutex m_storeMutex;
  tensorstore::TensorStore<> m_store;
};
