#include "FileReaderImageSequence.h"

#include "FileReaderTIFF.h"
#include "Logging.h"

#include <algorithm>
#include <filesystem>

std::vector<std::string>
initializeSequence(const std::string& filepath)
{
  std::filesystem::path fpath(filepath);
  // return a listing of all files in directory of filepath with same file extension
  std::vector<std::string> files;
  std::filesystem::path directory = fpath.parent_path();
  std::filesystem::path extension = fpath.extension();

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

FileReaderImageSequence::FileReaderImageSequence(const std::string& filepath)
  : m_tiffReader(new FileReaderTIFF(filepath))
{
  m_sequence = initializeSequence(filepath);
}

FileReaderImageSequence::~FileReaderImageSequence() {}

std::shared_ptr<ImageXYZC>
FileReaderImageSequence::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  if (loadSpec.time >= m_sequence.size()) {
    LOG_ERROR << "Time " << loadSpec.time << " exceeds image sequence length " << m_sequence.size();
    return {};
  }
  LoadSpec sequenceSpec = loadSpec;
  sequenceSpec.filepath = m_sequence[loadSpec.time];
  sequenceSpec.time = 0;
  // Forward the blocking call directly rather than going through loadFromFile,
  // so we stay on the worker thread we are already on and the TIFF reader sees
  // the same cancellation state.
  return m_tiffReader->loadVolumeBlocking(sequenceSpec, progress);
}

VolumeDimensions
FileReaderImageSequence::loadDimensions(const std::string& filepath, uint32_t scene)
{
  VolumeDimensions vd = m_tiffReader->loadDimensions(filepath, scene);
  vd.sizeT = m_sequence.size();
  return vd;
}

uint32_t
FileReaderImageSequence::loadNumScenes(const std::string& filepath)
{
  return m_tiffReader->loadNumScenes(filepath);
}

std::vector<MultiscaleDims>
FileReaderImageSequence::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
{
  std::vector<MultiscaleDims> dims = m_tiffReader->loadMultiscaleDims(filepath, scene);
  for (auto& d : dims) {
    d.shape[0] = m_sequence.size();
  }
  return dims;
}
