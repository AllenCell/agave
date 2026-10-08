#include "FileReaderDicom.h"

#include "ImageXYZC.h"
#include "Logging.h"

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcrledrg.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct DicomFolderIndex
{
  struct Slice
  {
    std::string path;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 1;
    uint16_t bits = 0;
    std::string photometric;
    float spacingX = 1.0f;
    float spacingY = 1.0f;
    float spacingZ = 1.0f;
    std::array<double, 3> position{};
    std::array<double, 6> orientation{};
    bool hasGeometry = false;
    int instance = 0;
    bool hasInstance = false;
    double projection = 0.0;
  };

  struct Series
  {
    std::vector<Slice> slices;
    VolumeDimensions dimensions;
  };

  std::vector<Series> series;
};

namespace {

void
registerDecoders()
{
  static std::once_flag once;
  std::call_once(once, [] {
    DcmRLEDecoderRegistration::registerCodecs();
    DJDecoderRegistration::registerCodecs();
    DJLSDecoderRegistration::registerCodecs();
  });
}

float
positiveSpacing(DcmDataset* dataset, const DcmTagKey& tag, unsigned long index = 0)
{
  Float64 value = 0.0;
  return dataset->findAndGetFloat64(tag, value, index).good() && std::isfinite(value) && value > 0.0
           ? static_cast<float>(value)
           : 1.0f;
}

float
sliceSpacing(DcmDataset* dataset)
{
  Float64 value = 0.0;
  if (dataset->findAndGetFloat64(DCM_SpacingBetweenSlices, value).good() && std::isfinite(value) && value > 0.0) {
    return static_cast<float>(value);
  }
  return positiveSpacing(dataset, DCM_SliceThickness);
}

bool
isDirectory(const std::string& filepath)
{
  std::error_code error;
  return std::filesystem::is_directory(filepath, error);
}

bool
readSliceMetadata(const std::filesystem::path& path, std::string& seriesUid, DicomFolderIndex::Slice& slice)
{
  DcmFileFormat file;
  if (file.loadFile(path.string().c_str()).bad()) {
    return false;
  }
  DcmDataset* dataset = file.getDataset();
  OFString uid;
  OFString photometric;
  Uint16 rows = 0;
  Uint16 columns = 0;
  Uint16 samples = 0;
  Uint16 bits = 0;
  if (dataset->findAndGetOFString(DCM_SeriesInstanceUID, uid).bad() || uid.empty() ||
      dataset->findAndGetUint16(DCM_Rows, rows).bad() || rows == 0 ||
      dataset->findAndGetUint16(DCM_Columns, columns).bad() || columns == 0 ||
      dataset->findAndGetUint16(DCM_SamplesPerPixel, samples).bad() || samples != 1 ||
      dataset->findAndGetUint16(DCM_BitsAllocated, bits).bad() || (bits != 8 && bits != 16) ||
      dataset->findAndGetOFString(DCM_PhotometricInterpretation, photometric).bad() ||
      (photometric != "MONOCHROME1" && photometric != "MONOCHROME2") || !dataset->tagExists(DCM_PixelData)) {
    return false;
  }

  slice.path = path.string();
  slice.width = columns;
  slice.height = rows;
  slice.bits = bits;
  slice.photometric = photometric.c_str();
  slice.spacingY = positiveSpacing(dataset, DCM_PixelSpacing, 0);
  slice.spacingX = positiveSpacing(dataset, DCM_PixelSpacing, 1);
  slice.spacingZ = sliceSpacing(dataset);
  OFString frames;
  if (dataset->findAndGetOFString(DCM_NumberOfFrames, frames).good()) {
    try {
      const unsigned long count = std::stoul(frames.c_str());
      if (count > std::numeric_limits<uint32_t>::max()) {
        return false;
      }
      slice.frames = static_cast<uint32_t>(count);
    } catch (const std::exception&) {
      return false;
    }
    if (slice.frames == 0) {
      return false;
    }
  }
  slice.hasGeometry = true;
  for (unsigned long i = 0; i < 3; ++i) {
    slice.hasGeometry &= dataset->findAndGetFloat64(DCM_ImagePositionPatient, slice.position[i], i).good();
    slice.hasGeometry &= std::isfinite(slice.position[i]);
  }
  for (unsigned long i = 0; i < 6; ++i) {
    slice.hasGeometry &= dataset->findAndGetFloat64(DCM_ImageOrientationPatient, slice.orientation[i], i).good();
    slice.hasGeometry &= std::isfinite(slice.orientation[i]);
  }
  OFString instance;
  if (dataset->findAndGetOFString(DCM_InstanceNumber, instance).good()) {
    try {
      slice.instance = std::stoi(instance.c_str());
      slice.hasInstance = true;
    } catch (const std::exception&) {
    }
  }
  seriesUid = uid.c_str();
  return true;
}

std::shared_ptr<DicomFolderIndex>
scanFolder(const std::string& filepath)
{
  auto index = std::make_shared<DicomFolderIndex>();
  std::map<std::string, std::vector<DicomFolderIndex::Slice>> grouped;
  std::error_code error;
  std::filesystem::recursive_directory_iterator it(
    filepath, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) {
      error.clear();
      continue;
    }
    std::string uid;
    DicomFolderIndex::Slice slice;
    if (readSliceMetadata(it->path(), uid, slice)) {
      grouped[uid].push_back(std::move(slice));
    }
  }

  for (auto& group : grouped) {
    const auto& uid = group.first;
    auto& slices = group.second;
    if (slices.empty() || slices.size() > std::numeric_limits<uint32_t>::max()) {
      continue;
    }
    const auto first = slices.front();
    bool consistent = std::all_of(slices.begin(), slices.end(), [&](const auto& slice) {
      return slice.width == first.width && slice.height == first.height && slice.bits == first.bits &&
             slice.photometric == first.photometric && std::abs(slice.spacingX - first.spacingX) < 0.001f &&
             std::abs(slice.spacingY - first.spacingY) < 0.001f && (slices.size() == 1 || slice.frames == 1);
    });
    if (!consistent) {
      LOG_WARNING << "Skipping DICOM series with incompatible slices: " << uid;
      continue;
    }

    float spacingZ = first.spacingZ;
    const bool spatial = std::all_of(slices.begin(), slices.end(), [](const auto& slice) { return slice.hasGeometry; });
    if (spatial && slices.size() > 1) {
      const auto& o = first.orientation;
      std::array<double, 3> normal = { o[1] * o[5] - o[2] * o[4],
                                       o[2] * o[3] - o[0] * o[5],
                                       o[0] * o[4] - o[1] * o[3] };
      const double norm = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
      if (norm <= 0.9) {
        LOG_WARNING << "Skipping DICOM series with invalid orientation: " << uid;
        continue;
      }
      for (auto& slice : slices) {
        for (size_t i = 0; i < 6; ++i) {
          consistent &= std::abs(slice.orientation[i] - o[i]) < 0.001;
        }
        slice.projection =
          (slice.position[0] * normal[0] + slice.position[1] * normal[1] + slice.position[2] * normal[2]) / norm;
      }
      if (!consistent) {
        LOG_WARNING << "Skipping DICOM series with inconsistent orientation: " << uid;
        continue;
      }
      std::sort(slices.begin(), slices.end(), [](const auto& a, const auto& b) { return a.projection < b.projection; });
      const double step = slices[1].projection - slices[0].projection;
      consistent = step > 0.001;
      for (size_t i = 2; i < slices.size(); ++i) {
        const double delta = slices[i].projection - slices[i - 1].projection;
        consistent &= delta > 0.001 && std::abs(delta - step) <= std::max(0.1, step * 0.05);
      }
      if (!consistent) {
        LOG_WARNING << "Skipping DICOM series with irregular slice positions: " << uid;
        continue;
      }
      spacingZ = static_cast<float>(step);
    } else {
      const bool numbered =
        std::all_of(slices.begin(), slices.end(), [](const auto& slice) { return slice.hasInstance; });
      if (numbered) {
        std::sort(slices.begin(), slices.end(), [](const auto& a, const auto& b) {
          return a.instance == b.instance ? a.path < b.path : a.instance < b.instance;
        });
        if (std::adjacent_find(slices.begin(), slices.end(), [](const auto& a, const auto& b) {
              return a.instance == b.instance;
            }) != slices.end()) {
          LOG_WARNING << "Skipping DICOM series with duplicate instance numbers and no slice positions: " << uid;
          continue;
        }
      } else {
        std::sort(slices.begin(), slices.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        if (slices.size() > 1) {
          LOG_WARNING << "DICOM series lacks slice positions and instance numbers; sorting by filename: " << uid;
        }
      }
    }

    DicomFolderIndex::Series series;
    series.dimensions.sizeX = first.width;
    series.dimensions.sizeY = first.height;
    series.dimensions.sizeZ = slices.size() == 1 ? first.frames : static_cast<uint32_t>(slices.size());
    series.dimensions.sizeC = 1;
    series.dimensions.sizeT = 1;
    series.dimensions.bitsPerPixel = ImageXYZC::IN_MEMORY_BPP;
    series.dimensions.channelNames = { "Intensity" };
    series.dimensions.spatialUnits = "mm";
    series.dimensions.physicalSizeX = first.spacingX;
    series.dimensions.physicalSizeY = first.spacingY;
    series.dimensions.physicalSizeZ = spacingZ;
    series.slices = std::move(slices);
    index->series.push_back(std::move(series));
  }
  return index;
}

bool
readDimensions(const std::string& filepath, VolumeDimensions& dims)
{
  registerDecoders();
  DcmFileFormat file;
  if (file.loadFile(filepath.c_str()).bad()) {
    LOG_ERROR << "Could not open DICOM file: " << filepath;
    return false;
  }
  DcmDataset* dataset = file.getDataset();
  Uint16 samples = 0;
  Uint16 bits = 0;
  OFString photometric;
  if (dataset->findAndGetUint16(DCM_SamplesPerPixel, samples).bad() || samples != 1 ||
      dataset->findAndGetUint16(DCM_BitsAllocated, bits).bad() || (bits != 8 && bits != 16) ||
      dataset->findAndGetOFString(DCM_PhotometricInterpretation, photometric).bad() ||
      (photometric != "MONOCHROME1" && photometric != "MONOCHROME2")) {
    LOG_ERROR << "DICOM reader supports 8 or 16 bit grayscale images: " << filepath;
    return false;
  }

  DicomImage image(filepath.c_str());
  if (image.getStatus() != EIS_Normal || image.getWidth() == 0 || image.getHeight() == 0 ||
      image.getFrameCount() == 0 || image.getWidth() > std::numeric_limits<uint32_t>::max() ||
      image.getHeight() > std::numeric_limits<uint32_t>::max() ||
      image.getFrameCount() > std::numeric_limits<uint32_t>::max()) {
    LOG_ERROR << "Could not decode DICOM image: " << filepath;
    return false;
  }

  dims.sizeX = static_cast<uint32_t>(image.getWidth());
  dims.sizeY = static_cast<uint32_t>(image.getHeight());
  dims.sizeZ = static_cast<uint32_t>(image.getFrameCount());
  dims.sizeC = 1;
  dims.sizeT = 1;
  dims.bitsPerPixel = ImageXYZC::IN_MEMORY_BPP;
  dims.channelNames = { "Intensity" };
  dims.spatialUnits = "mm";
  // Pixel Spacing stores row spacing before column spacing.
  dims.physicalSizeY = positiveSpacing(dataset, DCM_PixelSpacing, 0);
  dims.physicalSizeX = positiveSpacing(dataset, DCM_PixelSpacing, 1);
  dims.physicalSizeZ = sliceSpacing(dataset);
  return true;
}

} // namespace

FileReaderDicom::FileReaderDicom(const std::string& filepath)
{
  (void)filepath;
}

FileReaderDicom::~FileReaderDicom() = default;

std::shared_ptr<const DicomFolderIndex>
FileReaderDicom::folderIndex(const std::string& filepath)
{
  std::lock_guard<std::mutex> lock(m_indexMutex);
  if (!m_index || m_indexedPath != filepath) {
    m_index = scanFolder(filepath);
    m_indexedPath = filepath;
  }
  return m_index;
}

uint32_t
FileReaderDicom::loadNumScenes(const std::string& filepath)
{
  if (isDirectory(filepath)) {
    return static_cast<uint32_t>(folderIndex(filepath)->series.size());
  }
  VolumeDimensions dims;
  return readDimensions(filepath, dims) ? 1 : 0;
}

VolumeDimensions
FileReaderDicom::loadDimensions(const std::string& filepath, uint32_t scene)
{
  if (isDirectory(filepath)) {
    const auto index = folderIndex(filepath);
    return scene < index->series.size() ? index->series[scene].dimensions : VolumeDimensions{};
  }
  VolumeDimensions dims;
  if (scene != 0 || !readDimensions(filepath, dims)) {
    return {};
  }
  return dims;
}

std::vector<MultiscaleDims>
FileReaderDicom::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
{
  if (scene >= loadNumScenes(filepath)) {
    return {};
  }
  const VolumeDimensions dims = loadDimensions(filepath, scene);
  MultiscaleDims scale;
  scale.shape = { 1, 1, dims.sizeZ, dims.sizeY, dims.sizeX };
  scale.scale = { 1.0f, 1.0f, dims.physicalSizeZ, dims.physicalSizeY, dims.physicalSizeX };
  scale.dimensionOrder = { "T", "C", "Z", "Y", "X" };
  scale.dtype = "uint16";
  scale.channelNames = dims.channelNames;
  scale.spatialUnits = dims.spatialUnits;
  return { scale };
}

std::shared_ptr<ImageXYZC>
FileReaderDicom::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  if (loadSpec.time != 0 ||
      (!loadSpec.channels.empty() && (loadSpec.channels.size() != 1 || loadSpec.channels[0] != 0))) {
    return {};
  }
  VolumeDimensions dims;
  std::vector<std::string> files;
  if (isDirectory(loadSpec.filepath)) {
    const auto index = folderIndex(loadSpec.filepath);
    if (loadSpec.scene >= index->series.size()) {
      return {};
    }
    const auto& series = index->series[loadSpec.scene];
    dims = series.dimensions;
    for (const auto& slice : series.slices) {
      files.push_back(slice.path);
    }
  } else {
    if (loadSpec.scene != 0 || !readDimensions(loadSpec.filepath, dims)) {
      return {};
    }
    files.push_back(loadSpec.filepath);
  }
  const size_t planePixels = static_cast<size_t>(dims.sizeX) * dims.sizeY;
  if (planePixels > std::numeric_limits<size_t>::max() / sizeof(uint16_t) / dims.sizeZ) {
    LOG_ERROR << "DICOM image is too large: " << loadSpec.filepath;
    return {};
  }
  double globalMin = std::numeric_limits<double>::infinity();
  double globalMax = -std::numeric_limits<double>::infinity();
  for (const auto& path : files) {
    if (progress.isCancelled()) {
      return {};
    }
    DicomImage image(path.c_str());
    double imageMin = 0.0;
    double imageMax = 0.0;
    if (image.getStatus() != EIS_Normal || !image.getMinMaxValues(imageMin, imageMax)) {
      LOG_ERROR << "Could not decode DICOM image: " << path;
      return {};
    }
    globalMin = std::min(globalMin, imageMin);
    globalMax = std::max(globalMax, imageMax);
  }
  auto data = std::make_unique<uint8_t[]>(planePixels * dims.sizeZ * sizeof(uint16_t));
  const double width = std::max(1.0, globalMax - globalMin + 1.0);
  const double center = (globalMin + globalMax + 1.0) / 2.0;
  uint32_t z = 0;
  for (const auto& path : files) {
    DicomImage image(path.c_str());
    if (image.getStatus() != EIS_Normal || !image.setWindow(center, width) || image.getWidth() != dims.sizeX ||
        image.getHeight() != dims.sizeY || image.getFrameCount() > dims.sizeZ - z) {
      LOG_ERROR << "DICOM series changed while loading: " << path;
      return {};
    }
    for (unsigned long frameIndex = 0; frameIndex < image.getFrameCount(); ++frameIndex, ++z) {
      if (progress.isCancelled()) {
        return {};
      }
      const void* frame = image.getOutputData(16, frameIndex);
      if (!frame) {
        LOG_ERROR << "Could not decode DICOM frame " << frameIndex << " in " << path;
        return {};
      }
      std::memcpy(
        data.get() + static_cast<size_t>(z) * planePixels * sizeof(uint16_t), frame, planePixels * sizeof(uint16_t));
      progress.setProgress(z + 1, dims.sizeZ);
    }
  }
  if (z != dims.sizeZ) {
    LOG_ERROR << "DICOM series changed while loading: " << loadSpec.filepath;
    return {};
  }
  auto result = std::make_shared<ImageXYZC>(dims.sizeX,
                                            dims.sizeY,
                                            dims.sizeZ,
                                            1,
                                            ImageXYZC::IN_MEMORY_BPP,
                                            data.release(),
                                            dims.physicalSizeX,
                                            dims.physicalSizeY,
                                            dims.physicalSizeZ,
                                            dims.spatialUnits);
  result->setChannelNames(dims.channelNames);
  return result;
}
