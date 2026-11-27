#pragma once

#include "BlockingFileReader.h"
#include "VolumeDimensions.h"

#include <memory>
#include <mutex>
#include <string>

struct DicomFolderIndex;

// Reads a grayscale DICOM object or a directory containing one or more series.
class FileReaderDicom : public BlockingFileReader
{
public:
  explicit FileReaderDicom(const std::string& filepath);
  ~FileReaderDicom() override;

  bool supportChunkedLoading() const override { return false; }
  uint32_t loadNumScenes(const std::string& filepath) override;
  VolumeDimensions loadDimensions(const std::string& filepath, uint32_t scene = 0) override;
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string& filepath, uint32_t scene = 0) override;
  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override;

private:
  std::shared_ptr<const DicomFolderIndex> folderIndex(const std::string& filepath);

  std::mutex m_indexMutex;
  std::string m_indexedPath;
  std::shared_ptr<const DicomFolderIndex> m_index;
};
