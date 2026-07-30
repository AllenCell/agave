#include "FileReaderCzi.h"

#include "BoundingBox.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "VolumeDimensions.h"

#include <libCZI/Src/libCZI/libCZI.h>

#include "pugixml/pugixml.hpp"

#include <filesystem>

#include <chrono>
#include <codecvt>
#include <map>
#include <set>

FileReaderCzi::FileReaderCzi(const std::string& filepath) {}

FileReaderCzi::~FileReaderCzi()
{
  if (m_reader) {
    m_reader->Close();
  }
}

std::shared_ptr<libCZI::ICZIReader>
FileReaderCzi::openReader(const std::string& filepath)
{
  std::lock_guard<std::mutex> lock(m_readerMutex);

  if (m_reader && m_openPath == filepath) {
    return m_reader;
  }

  if (m_reader) {
    // A reader instance is normally used for a single file; handle a change of
    // path rather than silently returning data from the wrong one.
    m_reader->Close();
    m_reader.reset();
    m_dims.clear();
  }

  std::filesystem::path fpath(filepath);
  const std::wstring widestr = fpath.wstring();
  std::shared_ptr<libCZI::IStream> stream = libCZI::CreateStreamFromFile(widestr.c_str());
  std::shared_ptr<libCZI::ICZIReader> reader = libCZI::CreateCZIReader();
  reader->Open(stream);

  m_reader = reader;
  m_openPath = filepath;
  return m_reader;
}

libCZI::IntRect
getSceneYXSize(libCZI::SubBlockStatistics& statistics, int sceneIndex = 0)
{
  bool hasScene = statistics.dimBounds.IsValid(libCZI::DimensionIndex::S);
  if (hasScene) {
    int sStart(0), sSize(0);
    statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::S, &sStart, &sSize);
    if (sceneIndex >= sStart && (sStart + sSize - 1) >= sceneIndex && !statistics.sceneBoundingBoxes.empty()) {
      return statistics.sceneBoundingBoxes[sceneIndex].boundingBoxLayer0;
    }
  } else {
    return statistics.boundingBoxLayer0Only;
  }
  return statistics.boundingBoxLayer0Only;
}

pugi_agave::xml_node
firstChild(pugi_agave::xml_node& el, std::string tag)
{
  pugi_agave::xml_node child = el.child(tag.c_str());
  if (!child) {
    LOG_ERROR << "No " << tag << " element in xml";
  }
  return child;
}

bool
readCziDimensions(const std::shared_ptr<libCZI::ICZIReader>& reader,
                  const std::string filepath,
                  libCZI::SubBlockStatistics& statistics,
                  VolumeDimensions& dims,
                  uint32_t scene)
{
  // metadata xml
  auto mds = reader->ReadMetadataSegment();
  std::shared_ptr<libCZI::ICziMetadata> md = mds->CreateMetaFromMetadataSegment();
  std::shared_ptr<libCZI::ICziMultiDimensionDocumentInfo> docinfo = md->GetDocumentInfo();

  libCZI::ScalingInfoEx scalingInfo = docinfo->GetScalingInfoEx();
  // convert meters to microns?
  dims.physicalSizeX = (float)(scalingInfo.scaleX * 1000000.0);
  dims.physicalSizeY = (float)(scalingInfo.scaleY * 1000000.0);
  dims.physicalSizeZ = (float)(scalingInfo.scaleZ * 1000000.0);
  // just select the x unit for our units.
  using convert_typeX = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_typeX, wchar_t> converterX;
  dims.spatialUnits = VolumeDimensions::sanitizeUnitsString(converterX.to_bytes(scalingInfo.defaultUnitFormatX));

  // get all dimension bounds and enumerate.
  // I am making an assumption here that each scene has the same Z C and T dimensions.
  // If acquisition ended early, then the data might not be complete and that will have to be caught later.
  statistics.dimBounds.EnumValidDimensions([&](libCZI::DimensionIndex dimensionIndex, int start, int size) -> bool {
    switch (dimensionIndex) {
      case libCZI::DimensionIndex::Z:
        dims.sizeZ = size;
        break;
      case libCZI::DimensionIndex::C:
        dims.sizeC = size;
        break;
      case libCZI::DimensionIndex::T:
        dims.sizeT = size;
        break;
      default:
        break;
    }
    return true;
  });

  libCZI::IntRect planebox = getSceneYXSize(statistics, scene);
  dims.sizeX = planebox.w;
  dims.sizeY = planebox.h;

  std::string xml = md->GetXml();

  pugi_agave::xml_document czixml;
  pugi_agave::xml_parse_result parseOk = czixml.load_string(xml.c_str());
  if (!parseOk) {
    LOG_ERROR << "Bad CZI xml metadata content";
    return false;
  }

  pugi_agave::xml_node imageDocumentEl = firstChild(czixml, "ImageDocument");
  if (!imageDocumentEl) {
    return false;
  }
  pugi_agave::xml_node metadataEl = firstChild(imageDocumentEl, "Metadata");
  if (!metadataEl) {
    return false;
  }
  pugi_agave::xml_node informationEl = firstChild(metadataEl, "Information");
  if (!informationEl) {
    return false;
  }
  pugi_agave::xml_node imageEl = firstChild(informationEl, "Image");
  if (!imageEl) {
    return false;
  }
  pugi_agave::xml_node dimensionsEl = firstChild(imageEl, "Dimensions");
  if (!dimensionsEl) {
    return false;
  }
  pugi_agave::xml_node channelsEl = firstChild(dimensionsEl, "Channels");
  if (!channelsEl) {
    return false;
  }
  std::vector<std::string> channelNames;
  for (pugi_agave::xml_node node : channelsEl.children("Channel")) {
    channelNames.push_back(node.attribute("Name").value());
  }
  dims.channelNames = channelNames;

  libCZI::SubBlockInfo info;
  bool ok = reader->TryGetSubBlockInfoOfArbitrarySubBlockInChannel(0, info);
  if (ok) {
    switch (info.pixelType) {
      case libCZI::PixelType::Gray8:
        dims.bitsPerPixel = 8;
        break;
      case libCZI::PixelType::Gray16:
        dims.bitsPerPixel = 16;
        break;
      case libCZI::PixelType::Gray32Float:
        dims.bitsPerPixel = 32;
        break;
      case libCZI::PixelType::Bgr24:
        dims.bitsPerPixel = 24;
        break;
      case libCZI::PixelType::Bgr48:
        dims.bitsPerPixel = 48;
        break;
      case libCZI::PixelType::Bgr96Float:
        dims.bitsPerPixel = 96;
        break;
      default:
        dims.bitsPerPixel = 0;
        return false;
    }
  } else {
    return false;
  }

  return dims.validate();
}

// DANGER: assumes dataPtr has enough space allocated!!!!
bool
readCziPlane(const std::shared_ptr<libCZI::ICZIReader>& reader,
             const libCZI::IntRect& planeRect,
             const libCZI::CDimCoordinate& planeCoord,
             const VolumeDimensions& volumeDims,
             const libCZI::ISingleChannelPyramidLayerTileAccessor::Options* options,
             uint8_t* dataPtr)
{
  auto accessor = reader->CreateSingleChannelPyramidLayerTileAccessor();

  libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo pyrLyrInfo;
  pyrLyrInfo.minificationFactor = 1;
  pyrLyrInfo.pyramidLayerNo = 0;

  auto bitmap = accessor->Get(planeRect, &planeCoord, pyrLyrInfo, options);
  libCZI::IntSize size = bitmap->GetSize();
  {
    libCZI::ScopedBitmapLockerSP lckScoped{ bitmap };
    assert(lckScoped.ptrDataRoi == lckScoped.ptrData);
    assert(volumeDims.sizeX == size.w);
    assert(volumeDims.sizeY == size.h);
    size_t bytesPerRow = size.w * 2; // destination stride
    if (volumeDims.bitsPerPixel == 16) {
      assert(lckScoped.stride >= size.w * 2);
      // stridewise copying
      for (std::uint32_t y = 0; y < size.h; ++y) {
        const std::uint8_t* ptrLine = ((const std::uint8_t*)lckScoped.ptrDataRoi) + y * lckScoped.stride;
        // uint16 is 2 bytes per pixel
        memcpy(dataPtr + (bytesPerRow * y), ptrLine, bytesPerRow);
      }
    } else if (volumeDims.bitsPerPixel == 8) {
      assert(lckScoped.stride >= size.w);
      // stridewise copying
      for (std::uint32_t y = 0; y < size.h; ++y) {
        const std::uint8_t* ptrLine = ((const std::uint8_t*)lckScoped.ptrDataRoi) + y * lckScoped.stride;
        uint16_t* destLine = reinterpret_cast<uint16_t*>(dataPtr + (bytesPerRow * y));
        for (size_t x = 0; x < size.w; ++x) {
          *destLine++ = *(ptrLine + x);
        }
      }
    }
    // else do nothing.
    // buffer is already initialized to zero,
    // and dimension validation earlier should prevent anything unintentional here.
  }
  return true;
}

uint32_t
FileReaderCzi::loadNumScenes(const std::string& filepath)
{
  try {
    std::shared_ptr<libCZI::ICZIReader> cziReader = openReader(filepath);

    auto statistics = cziReader->GetStatistics();
    int sceneStart = 0;
    int sceneSize = 0;
    bool scenesDefined = statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::S, &sceneStart, &sceneSize);
    if (!scenesDefined) {
      // assume just one?
      return 1;
    }
    return sceneSize;

  } catch (std::exception& e) {
    LOG_ERROR << e.what();
    LOG_ERROR << "Failed to read " << filepath;
    return 0;
  } catch (...) {
    LOG_ERROR << "Failed to read " << filepath;
    return 0;
  }
  return 0;
}

bool
FileReaderCzi::cachedDimensions(const std::string& filepath, uint32_t scene, VolumeDimensions& dims)
{
  {
    std::lock_guard<std::mutex> lock(m_readerMutex);
    auto it = m_dims.find(scene);
    if (it != m_dims.end()) {
      dims = it->second;
      return true;
    }
  }

  std::shared_ptr<libCZI::ICZIReader> reader = openReader(filepath);
  auto statistics = reader->GetStatistics();
  VolumeDimensions read;
  if (!readCziDimensions(reader, filepath, statistics, read, scene)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_readerMutex);
    m_dims[scene] = read;
  }
  dims = read;
  return true;
}

VolumeDimensions
FileReaderCzi::loadDimensions(const std::string& filepath, uint32_t scene)
{
  try {
    VolumeDimensions dims;
    if (!cachedDimensions(filepath, scene, dims)) {
      return VolumeDimensions();
    }
    return dims;

  } catch (std::exception& e) {
    LOG_ERROR << e.what();
    LOG_ERROR << "Failed to read " << filepath;
    return VolumeDimensions();
  } catch (...) {
    LOG_ERROR << "Failed to read " << filepath;
    return VolumeDimensions();
  }
}

std::shared_ptr<ImageXYZC>
FileReaderCzi::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  std::string filepath = loadSpec.filepath;
  uint32_t scene = loadSpec.scene;
  uint32_t time = loadSpec.time;
  VolumeDimensions outDims;

  std::shared_ptr<ImageXYZC> emptyimage;

  auto tStart = std::chrono::high_resolution_clock::now();

  try {
    // Reuses the already-open reader and the memoized metadata, so a time series
    // pays the file-open and XML-parse cost once rather than per timepoint.
    std::shared_ptr<libCZI::ICZIReader> cziReader = openReader(filepath);

    auto statistics = cziReader->GetStatistics();

    VolumeDimensions dims;
    if (!cachedDimensions(filepath, scene, dims)) {
      return emptyimage;
    }
    int startT = 0, sizeT = 0;
    int startC = 0, sizeC = 0;
    int startZ = 0, sizeZ = 0;
    int startS = 0, sizeS = 0;
    bool hasT = statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::T, &startT, &sizeT);
    bool hasZ = statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::Z, &startZ, &sizeZ);
    bool hasC = statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::C, &startC, &sizeC);
    bool hasS = statistics.dimBounds.TryGetInterval(libCZI::DimensionIndex::S, &startS, &sizeS);

    if (!hasZ) {
      LOG_ERROR << "AGAVE can only read zstack volume data";
      return emptyimage;
    }
    if (dims.sizeC != sizeC || !hasC) {
      LOG_ERROR << "Inconsistent Channel count in czi file";
      return emptyimage;
    }

    size_t nch = loadSpec.channels.empty() ? dims.sizeC : loadSpec.channels.size();

    size_t planesize = (size_t)dims.sizeX * (size_t)dims.sizeY * (size_t)dims.bitsPerPixel / 8;
    uint8_t* data = new uint8_t[planesize * dims.sizeZ * nch];
    memset(data, 0, planesize * dims.sizeZ * nch);

    // stash it here in case of early exit, it will be deleted
    std::unique_ptr<uint8_t[]> smartPtr(data);

    uint8_t* destptr = data;

    // now ready to read channels one by one.
    libCZI::IntRect planeRect;
    if (hasS) {
      planeRect = statistics.sceneBoundingBoxes[startS + scene].boundingBoxLayer0;
    } else {
      planeRect = statistics.boundingBoxLayer0Only;
    }

    libCZI::ISingleChannelPyramidLayerTileAccessor::Options o;
    o.Clear();
    if (hasS) {
      std::wstringstream wss;
      wss << scene;
      o.sceneFilter = libCZI::Utils::IndexSetFromString(wss.str());
    }

    for (uint32_t channel = 0; channel < nch; ++channel) {
      uint32_t channelToLoad = channel;
      if (!loadSpec.channels.empty()) {
        channelToLoad = loadSpec.channels[channel];
      }

      for (uint32_t slice = 0; slice < dims.sizeZ; ++slice) {
        // Cancellation boundary: one subblock read per plane.
        if (progress.isCancelled()) {
          return emptyimage;
        }
        destptr = data + planesize * (channel * dims.sizeZ + slice);

        // adjust coordinates by offsets from dims
        libCZI::CDimCoordinate planeCoord{ { libCZI::DimensionIndex::Z, (int)slice + startZ } };
        if (hasC) {
          planeCoord.Set(libCZI::DimensionIndex::C, (int)channelToLoad + startC);
        }
        if (hasT) {
          planeCoord.Set(libCZI::DimensionIndex::T, time + startT);
        }
        // since scene tiles can not overlap, passing the scene bounding box in to readCziPlane is enough produce the
        // scene, and I don't need to add Scene to the planeCoord.

        if (!readCziPlane(cziReader, planeRect, planeCoord, dims, &o, destptr)) {
          return emptyimage;
        }
      }

      progress.setProgress(channel + 1, static_cast<uint32_t>(nch));
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = tEnd - tStart;
    LOG_DEBUG << "CZI loaded in " << (elapsed.count() * 1000.0) << "ms";

    auto tStartImage = std::chrono::high_resolution_clock::now();

    // TODO: convert data to uint16_t pixels if not already.
    // we can release the smartPtr because ImageXYZC will now own the raw data memory
    ImageXYZC* im = new ImageXYZC(dims.sizeX,
                                  dims.sizeY,
                                  dims.sizeZ,
                                  nch,
                                  ImageXYZC::IN_MEMORY_BPP, // dims.bitsPerPixel,
                                  smartPtr.release(),
                                  dims.physicalSizeX,
                                  dims.physicalSizeY,
                                  dims.physicalSizeZ,
                                  dims.spatialUnits);

    std::vector<std::string> channelNames = dims.getChannelNames(loadSpec.channels);
    im->setChannelNames(channelNames);

    tEnd = std::chrono::high_resolution_clock::now();
    elapsed = tEnd - tStartImage;
    LOG_DEBUG << "ImageXYZC prepared in " << (elapsed.count() * 1000.0) << "ms";

    elapsed = tEnd - tStart;
    LOG_DEBUG << "Loaded " << filepath << " in " << (elapsed.count() * 1000.0) << "ms";

    std::shared_ptr<ImageXYZC> sharedImage(im);
    outDims = dims;

    return sharedImage;

  } catch (std::exception& e) {
    LOG_ERROR << e.what();
    LOG_ERROR << "Failed to read " << filepath;
    return emptyimage;
  } catch (...) {
    LOG_ERROR << "Failed to read " << filepath;
    return emptyimage;
  }
}

std::vector<MultiscaleDims>
FileReaderCzi::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
{
  std::vector<MultiscaleDims> dims;
  VolumeDimensions vdims = loadDimensions(filepath, scene);
  if (!vdims.validate()) {
    return dims;
  }
  MultiscaleDims mdims;
  mdims.shape = { vdims.sizeT, vdims.sizeC, vdims.sizeZ, vdims.sizeY, vdims.sizeX };
  mdims.scale = { 1.0, 1.0, vdims.physicalSizeZ, vdims.physicalSizeY, vdims.physicalSizeX };
  mdims.dimensionOrder = { "T", "C", "Z", "Y", "X" };
  mdims.dtype = "uint16";
  mdims.path = "";
  mdims.channelNames = vdims.channelNames;
  dims.push_back(mdims);
  return dims;
}
