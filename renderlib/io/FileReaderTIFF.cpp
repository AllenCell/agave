#include "FileReaderTIFF.h"

#include "BoundingBox.h"
#include "ConvertChannelData.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "StringUtil.h"
#include "VolumeDimensions.h"

#include "pugixml/pugixml.hpp"

#include <tiff.h>
#include <tiffio.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>

FileReaderTIFF::FileReaderTIFF(const std::string& filepath) {}

FileReaderTIFF::~FileReaderTIFF() {}

class ScopedTiffReader
{
public:
  ScopedTiffReader(const std::string& filepath)
  {
    // Loads tiff file
    m_tiff = TIFFOpen(filepath.c_str(), "r");
    if (!m_tiff) {
      LOG_ERROR << "Failed to open TIFF: '" << filepath << "'";
    }
  }
  ~ScopedTiffReader()
  {
    if (m_tiff) {
      TIFFClose(m_tiff);
    }
  }
  TIFF* reader() { return m_tiff; }

protected:
  TIFF* m_tiff;
};

uint32_t
requireUint32Attr(pugi_agave::xml_node& el, const std::string& attr, uint32_t defaultVal)
{
  return (uint32_t)el.attribute(attr.c_str()).as_uint(defaultVal);
}

float
requireFloatAttr(pugi_agave::xml_node& el, const std::string& attr, float defaultVal)
{
  return el.attribute(attr.c_str()).as_float(defaultVal);
}

static int
readTiffNumScenes(TIFF* tiff, const std::string& filepath)
{
  int numscenes = 1;

  char* imagedescription = nullptr;
  // metadata is in ImageDescription of first IFD in the file.
  if (TIFFGetField(tiff, TIFFTAG_IMAGEDESCRIPTION, &imagedescription) != 1) {
    imagedescription = nullptr;
    LOG_WARNING << "Failed to read imagedescription of TIFF: '" << filepath << "';  interpreting as single channel.";
  }

  std::string simagedescription = trim(imagedescription ? imagedescription : "");

  // check for plain tiff with ImageJ imagedescription:
  if (startsWith(simagedescription, "ImageJ=")) {
    numscenes = 1;
  } else if (startsWith(simagedescription, "{\"shape\":")) {
    numscenes = 1;
  } else if ((startsWith(simagedescription, "<?xml version") || startsWith(simagedescription, "<OME xmlns")) &&
             endsWith(simagedescription, "OME>")) {
    // convert c to xml doc.  if this fails then we don't have an ome tif.
    pugi_agave::xml_document omexml;
    pugi_agave::xml_parse_result parseOk = omexml.load_string(simagedescription.c_str());
    if (!parseOk) {
      LOG_ERROR << "Bad OME xml metadata content";
      return false;
    }

    pugi_agave::xml_node omenode = omexml.child("OME");

    // count how many <Image> tags and that is our number of scenes.
    numscenes = 0;
    pugi_agave::xml_node imageEl;
    pugi_agave::xml_node pixelsEl;
    for (pugi_agave::xml_node imagenode : omenode.children("Image")) {
      numscenes++;
    }
  } else {
    numscenes = 1;
  }

  return numscenes;
}

uint32_t
FileReaderTIFF::loadNumScenes(const std::string& filepath)
{
  // Loads tiff file
  ScopedTiffReader tiffreader(filepath);
  TIFF* tiff = tiffreader.reader();
  if (!tiff) {
    return 0;
  }

  return readTiffNumScenes(tiff, filepath);
}

bool
readTiffDimensions(TIFF* tiff, const std::string filepath, VolumeDimensions& dims, uint32_t scene)
{
  char* imagedescription = nullptr;
  // metadata is in ImageDescription of first IFD in the file.
  if (TIFFGetField(tiff, TIFFTAG_IMAGEDESCRIPTION, &imagedescription) != 1) {
    imagedescription = nullptr;
    LOG_WARNING << "Failed to read imagedescription of TIFF: '" << filepath << "';  interpreting as single channel.";
  }

  // Temporary variables
  uint32_t width, height;
  //  tsize_t scanlength;

  // Read dimensions of image
  if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) != 1) {
    LOG_ERROR << "Failed to read width of TIFF: '" << filepath << "'";
    return false;
  }
  if (TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) != 1) {
    LOG_ERROR << "Failed to read height of TIFF: '" << filepath << "'";
    return false;
  }

  uint32_t bpp = 0;
  if (TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bpp) != 1) {
    LOG_ERROR << "Failed to read bpp of TIFF: '" << filepath << "'";
    return false;
  }

  uint16_t sampleFormat = SAMPLEFORMAT_UINT;
  if (TIFFGetField(tiff, TIFFTAG_SAMPLEFORMAT, &sampleFormat) != 1) {
    LOG_WARNING << "Failed to read sampleformat of TIFF: '" << filepath << "'";
  }
  if (sampleFormat != SAMPLEFORMAT_UINT && sampleFormat != SAMPLEFORMAT_IEEEFP && sampleFormat != SAMPLEFORMAT_INT) {
    LOG_ERROR << "Unsupported tiff SAMPLEFORMAT " << sampleFormat << " for '" << filepath << "'";
    return false;
  }

  uint16_t photometric = PHOTOMETRIC_MINISBLACK;
  if (TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) != 1) {
    LOG_WARNING << "Failed to read photometric of TIFF: '" << filepath << "'";
  }

  uint32_t sizeT = 1;
  uint32_t sizeX = width;
  uint32_t sizeY = height;
  uint32_t sizeZ = 1;
  uint32_t sizeC = 1;
  float physicalSizeX = 1.0f;
  float physicalSizeY = 1.0f;
  float physicalSizeZ = 1.0f;
  std::string units = "units";
  float timeUnit = 1.0f;
  std::string timeUnits = "s";
  // see if we can glean xy resolution from the tiff tags
  float tiffXResolution = 1.0f;
  if (TIFFGetField(tiff, TIFFTAG_XRESOLUTION, &tiffXResolution) == 1) {
    physicalSizeX = 1.0f / tiffXResolution;
  }
  float tiffYResolution = 1.0f;
  if (TIFFGetField(tiff, TIFFTAG_YRESOLUTION, &tiffYResolution) == 1) {
    physicalSizeY = 1.0f / tiffYResolution;
  }

  std::vector<std::string> channelNames;
  std::string dimensionOrder = "XYCZT";

  std::string simagedescription = trim(imagedescription ? imagedescription : "");

  // check for plain tiff with ImageJ imagedescription:
  if (startsWith(simagedescription, "ImageJ=")) {
    // "ImageJ=\nhyperstack=true\nimages=7900\nchannels=1\nslices=50\nframes=158"
    // "ImageJ=1.52i\nimages=126\nchannels=2\nslices=63\nhyperstack=true\nmode=composite\nunit=
    //      micron\nfinterval=299.2315368652344\nspacing=0.2245383462882669\nloop=false\nmin=9768.0\nmax=
    //        14591.0\n"
    std::map<std::string, std::string> imagejmetadata = splitToNameValuePairs(simagedescription);
    if (imagejmetadata.empty()) {
      LOG_ERROR << "Unexpected bad or empty TIFF ImageJ metadata";
      return false;
    }

    auto iter = imagejmetadata.find("channels");
    if (iter != imagejmetadata.end()) {
      sizeC = std::stoi((*iter).second);
    } else {
      LOG_WARNING << "Failed to read number of channels of ImageJ TIFF: '" << filepath << "'";
    }

    iter = imagejmetadata.find("slices");
    if (iter != imagejmetadata.end()) {
      sizeZ = std::stoi((*iter).second);
    } else {
      LOG_WARNING << "Failed to read number of slices of ImageJ TIFF: '" << filepath << "'";
    }

    iter = imagejmetadata.find("frames");
    if (iter != imagejmetadata.end()) {
      sizeT = std::stoi((*iter).second);
    } else {
      LOG_WARNING << "Failed to read number of frames of ImageJ TIFF: '" << filepath << "'";
    }

    iter = imagejmetadata.find("finterval");
    if (iter != imagejmetadata.end()) {
      try {
        timeUnit = std::stof((*iter).second);
        if (timeUnit < 0.0f) {
          LOG_WARNING << "Found unexpected negative frame interval in ImageJ TIFF: '" << filepath
                      << "', negating for a positive value";
          timeUnit = -timeUnit;
        }
        if (timeUnit == 0.0f) {
          LOG_WARNING << "Frame interval of zero in ImageJ TIFF: '" << filepath
                      << "', using frame interval of 1.0 instead";
          timeUnit = 1.0f;
        }
      } catch (...) {
        LOG_WARNING << "Failed to read frame interval of ImageJ TIFF: '" << filepath << "'";
        timeUnit = 1.0f;
      }
    }

    iter = imagejmetadata.find("spacing");
    if (iter != imagejmetadata.end()) {
      try {
        physicalSizeZ = std::stof((*iter).second);
        if (physicalSizeZ < 0.0f) {
          physicalSizeZ = -physicalSizeZ;
        }
      } catch (...) {
        LOG_WARNING << "Failed to read spacing of ImageJ TIFF: '" << filepath << "'";
        physicalSizeZ = 1.0f;
      }
    }

    iter = imagejmetadata.find("unit");
    if (iter != imagejmetadata.end()) {
      units = (*iter).second;
    } else {
      LOG_WARNING << "Failed to read spatial units of ImageJ TIFF: '" << filepath << "'";
    }

    for (uint32_t i = 0; i < sizeC; ++i) {
      channelNames.push_back(std::to_string(i));
    }
  } else if (startsWith(simagedescription, "SCIFIO=")) {
    // SCIFIO=0.41.0
    // axes=X,Y,Unknown
    // lengths=725,694,906
    // scales=1.0,1.0,1.0
    // units=null,null,null
    // bitsPerPixel=8
    // images=906
    // channels=1
    // slices=1
    // frames=1
    // hyperstack=true
    // mode=composite
    // unit=null

    std::map<std::string, std::string> scifiometadata = splitToNameValuePairs(simagedescription);
    if (scifiometadata.empty()) {
      LOG_ERROR << "Unexpected bad or empty TIFF SCIFIO metadata";
      return false;
    }

    std::vector<std::string> axes;
    auto iter = scifiometadata.find("axes");
    if (iter != scifiometadata.end()) {
      split((*iter).second, ',', axes);
    } else {
      LOG_WARNING << "Failed to read axes of SCIFIO TIFF: '" << filepath << "'";
    }
    std::vector<std::string> scales;
    iter = scifiometadata.find("scales");
    if (iter != scifiometadata.end()) {
      split((*iter).second, ',', scales);
    } else {
      LOG_WARNING << "Failed to read scales of SCIFIO TIFF: '" << filepath << "'";
    }
    std::vector<std::string> lengths;
    iter = scifiometadata.find("lengths");
    if (iter != scifiometadata.end()) {
      split((*iter).second, ',', lengths);
    } else {
      LOG_WARNING << "Failed to read lengths of SCIFIO TIFF: '" << filepath << "'";
    }
    std::vector<std::string> unitsArray;
    iter = scifiometadata.find("units");
    if (iter != scifiometadata.end()) {
      split((*iter).second, ',', unitsArray);
      // just take the first one for now?
      units = unitsArray[0];
      if (units == "null") {
        units = "units";
      }
    } else {
      LOG_WARNING << "Failed to read units of SCIFIO TIFF: '" << filepath << "'";
    }

    if (lengths.size() != unitsArray.size() || scales.size() != axes.size() || scales.size() != unitsArray.size()) {
      LOG_ERROR << "SCIFIO TIFF metadata has inconsistent counts of lengths,units,axes,scales";
    }

    for (size_t i = 0; i < axes.size(); ++i) {
      std::string axis = axes[i];
      double scale = std::stod(scales[i]);
      std::string unit = unitsArray[i];
      int length = std::stoi(lengths[i]);
      if (axis == "X") {
        // check consistency.
        if (sizeX != length) {
          LOG_WARNING << "Inconsistent X size " << length << " in SCIFIO TIFF: '" << filepath << "'";
        }
        physicalSizeX = scale;
      } else if (axis == "Y") {
        if (sizeY != length) {
          LOG_WARNING << "Inconsistent Y size " << length << " in SCIFIO TIFF: '" << filepath << "'";
        }
        physicalSizeY = scale;
      } else if (axis == "Z") {
        sizeZ = length;
        physicalSizeZ = scale;
      } else if (axis == "Channel") {
        sizeC = length;
      } else if (axis == "Time") {
        sizeT = length;
      } else if (axis == "Unknown" && axes.size() == 3) {
        // make a guess and use the last axis as Z
        sizeZ = length;
        physicalSizeZ = scale;
      }
    }
    iter = scifiometadata.find("channels");
    if (iter != scifiometadata.end()) {
      int nch = std::stoi((*iter).second);
      if (sizeC != nch) {
        LOG_WARNING << "Inconsistent number of channels " << nch << " in SCIFIO TIFF: '" << filepath << "'";
      }
    } else {
      LOG_WARNING << "Failed to read number of channels of SCIFIO TIFF: '" << filepath << "'";
    }

    for (uint32_t i = 0; i < sizeC; ++i) {
      channelNames.push_back(std::to_string(i));
    }

  } else if (startsWith(simagedescription, "{\"shape\":")) {
    // expect a 4d shape array of C,Z,Y,X or 5d T,C,Z,Y,X, or 3d Z,Y,X, or 2d Y,X
    size_t firstBracket = simagedescription.find('[');
    size_t lastBracket = simagedescription.rfind(']');
    std::string shape = simagedescription.substr(firstBracket + 1, lastBracket - firstBracket - 1);
    LOG_INFO << shape;
    std::vector<std::string> shapelist;
    split(shape, ',', shapelist);
    if ((shapelist.size() > 5) || (shapelist.size() < 2)) {
      LOG_ERROR << "Expected shape to be 2D, 3D, 4D, or 5D TIFF: '" << filepath << "'";
      return false;
    }
    dimensionOrder = "XYZCT";
    // basically, if the shape has
    // 5 elements, then it is T,C,Z,Y,X
    // 4 elements, then it is C,Z,Y,X
    // 3 elements, then it is Z,Y,X
    // 2 elements, then it is Y,X
    sizeT = 1;
    sizeC = 1;
    sizeZ = 1;
    sizeY = 1;
    sizeX = 1;

    // reverse the shapelist
    std::reverse(shapelist.begin(), shapelist.end());
    sizeX = std::stoi(shapelist[0]);
    sizeY = std::stoi(shapelist[1]);
    if (shapelist.size() > 2) {
      sizeZ = std::stoi(shapelist[2]);
    }
    if (shapelist.size() > 3) {
      sizeC = std::stoi(shapelist[3]);
    }
    if (shapelist.size() > 4) {
      sizeT = std::stoi(shapelist[4]);
    }
    for (uint32_t i = 0; i < sizeC; ++i) {
      channelNames.push_back(std::to_string(i));
    }
    LOG_DEBUG << "Parsed shape: " << sizeT << "," << sizeC << "," << sizeZ << "," << sizeY << "," << sizeX;
  } else if ((startsWith(simagedescription, "<?xml version") || startsWith(simagedescription, "<OME xmlns")) &&
             endsWith(simagedescription, "OME>")) {
    // convert c to xml doc.  if this fails then we don't have an ome tif.
    pugi_agave::xml_document omexml;
    pugi_agave::xml_parse_result parseOk = omexml.load_string(simagedescription.c_str());
    if (!parseOk) {
      LOG_ERROR << "Bad OME xml metadata content";
      return false;
    }

    pugi_agave::xml_node omenode = omexml.child("OME");

    auto binaryonlynode = omenode.child("BinaryOnly");
    if (binaryonlynode) {
      // try to get metadata from the MetadataFile attribute
      std::string metadatafile = binaryonlynode.attribute("MetadataFile").as_string("");
      if (metadatafile.empty()) {
        LOG_ERROR << "BinaryOnly OME TIFFs is missing a MetadataFile attribute: '" << filepath << "'";
        return false;
      }
      // try to load the metadata file.
      // try prefixing the metadatafile with the directory of the tiff.
      std::filesystem::path tiffpath(filepath);
      std::filesystem::path tiffdir = tiffpath.parent_path();
      std::string metadatapath = (tiffdir / metadatafile).string();

      pugi_agave::xml_document metadataxml;
      pugi_agave::xml_parse_result parseOk = metadataxml.load_file(metadatapath.c_str());
      if (!parseOk) {
        LOG_ERROR << "Failed to load metadata file: '" << metadatafile << "' for OME TIFF: '" << filepath << "'";
        return false;
      }
      omenode = metadataxml.child("OME");
      // then continue with the metadata in the metadata file.
    }

    // extract some necessary info from the xml:

    // count how many <Image> tags and that is our number of scenes.
    uint32_t numScenes = 0;
    pugi_agave::xml_node imageEl;
    pugi_agave::xml_node pixelsEl;
    auto imagenodes = omenode.children("Image");
    numScenes = std::distance(imagenodes.begin(), imagenodes.end());
    if (scene >= numScenes) {
      LOG_ERROR << "Requested invalid scene index " << scene << " in OME TIFF; returning scene 0";
      scene = 0;
    }
    auto imageElIterator = imagenodes.begin();
    for (uint32_t i = 0; i < scene; ++i) {
      ++imageElIterator;
    }
    imageEl = *imageElIterator;
    pixelsEl = imageEl.child("Pixels");
    // for (pugi_agave::xml_node imagenode : imagenodes) {
    //   // get the Image and Pixels element of scene
    //   if (numScenes == scene) {
    //     imageEl = imagenode;
    //     pixelsEl = imagenode.child("Pixels");
    //   }
    // }

    if (!imageEl || !pixelsEl) {
      LOG_ERROR << "No <Pixels> element in ome xml for scene " << scene;
      return false;
    }

    // skipping "complex", "double-complex", and "bit".
    std::map<std::string, uint32_t> mapPixelTypeBPP = { { "uint8", 8 },  { "uint16", 16 }, { "uint32", 32 },
                                                        { "int8", 8 },   { "int16", 16 },  { "int32", 32 },
                                                        { "float", 32 }, { "double", 64 } };

    std::string pixelType = pixelsEl.attribute("Type").as_string("uint16");
    std::transform(
      pixelType.begin(), pixelType.end(), pixelType.begin(), [](unsigned char c) { return std::tolower(c); });
    LOG_INFO << "pixel type: " << pixelType;
    bpp = mapPixelTypeBPP[pixelType];
    if (bpp != 32 && bpp != 16 && bpp != 8) {
      LOG_ERROR << "Image must be 8 or 16-bit integer, or 32-bit float typed";
      return false;
    }
    sizeX = requireUint32Attr(pixelsEl, "SizeX", 0);
    sizeY = requireUint32Attr(pixelsEl, "SizeY", 0);
    sizeZ = requireUint32Attr(pixelsEl, "SizeZ", 0);
    sizeC = requireUint32Attr(pixelsEl, "SizeC", 0);
    sizeT = requireUint32Attr(pixelsEl, "SizeT", 0);
    // one of : "XYZCT", "XYZTC","XYCTZ","XYCZT","XYTCZ","XYTZC"
    dimensionOrder = pixelsEl.attribute("DimensionOrder").as_string(dimensionOrder.c_str());
    physicalSizeX = requireFloatAttr(pixelsEl, "PhysicalSizeX", 1.0f);
    physicalSizeY = requireFloatAttr(pixelsEl, "PhysicalSizeY", 1.0f);
    physicalSizeZ = requireFloatAttr(pixelsEl, "PhysicalSizeZ", 1.0f);
    std::string physicalSizeXunit = pixelsEl.attribute("PhysicalSizeXUnit").as_string("");
    std::string physicalSizeYunit = pixelsEl.attribute("PhysicalSizeYUnit").as_string("");
    std::string physicalSizeZunit = pixelsEl.attribute("PhysicalSizeZUnit").as_string("");
    units = physicalSizeXunit;
    if (physicalSizeYunit != units || physicalSizeZunit != units) {
      LOG_WARNING << "Inconsistent physical size units in OME TIFF: '" << filepath << "' " << physicalSizeXunit << " "
                  << physicalSizeYunit << " " << physicalSizeZunit;
    }
    // this is the documented ome-xml default.
    if (units.empty()) {
      units = "um";
    }

    // find channel names
    int i = 0;
    for (pugi_agave::xml_node node : pixelsEl.children("Channel")) {
      std::string chid = node.attribute("ID").value();
      std::string chname = node.attribute("Name").value();
      if (!chname.empty()) {
        channelNames.push_back(chname);
      } else if (!chid.empty()) {
        channelNames.push_back(chid);
      } else {
        channelNames.push_back(std::to_string(i));
      }
      i++;
    }
  } else {
    // unrecognized string / no metadata.
    // walk the file and count the directories and assume that is Z
    // sizeZ was initialized to 1.
    sizeZ = 0;
    while (TIFFSetDirectory(tiff, sizeZ)) {
      sizeZ++;
    };
    channelNames.push_back("0");
  }

  assert(sizeX == width);
  assert(sizeY == height);

  // allocate the destination buffer!!!!
  assert(sizeT >= 1);
  assert(sizeC >= 1);
  assert(sizeX >= 1);
  assert(sizeY >= 1);
  assert(sizeZ >= 1);

  dims.sizeX = sizeX;
  dims.sizeY = sizeY;
  dims.sizeZ = sizeZ;
  dims.sizeC = sizeC;
  dims.sizeT = sizeT;
  dims.dimensionOrder = dimensionOrder;
  dims.physicalSizeX = physicalSizeX;
  dims.physicalSizeY = physicalSizeY;
  dims.physicalSizeZ = physicalSizeZ;
  dims.spatialUnits = VolumeDimensions::sanitizeUnitsString(units);
  dims.timeUnit = timeUnit;
  dims.timeUnits = VolumeDimensions::sanitizeUnitsString(timeUnits);
  dims.bitsPerPixel = bpp;
  dims.channelNames = channelNames;
  dims.sampleFormat = sampleFormat;

  dims.log();

  return dims.validate();
}

// return number of bytes copied to dest
size_t
copyDirect(uint8_t* dest, const uint8_t* src, size_t numBytes, int srcBitsPerPixel)
{
  memcpy(dest, src, numBytes);
  return numBytes;
}

// DANGER: assumes dataPtr has enough space allocated!!!!
bool
readTiffPlane(TIFF* tiff, int planeIndex, const VolumeDimensions& dims, uint8_t* dataPtr)
{
  int setdirok = TIFFSetDirectory(tiff, planeIndex);
  if (setdirok == 0) {
    LOG_ERROR << "Bad tiff directory specified: " << (planeIndex);
    return false;
  }

  tmsize_t numBytesRead = 0;
  // TODO future optimize:
  // This function is usually called in a loop. We could factor out the TIFFmalloc and TIFFfree calls.
  // Should profile to see if the repeated malloc/frees are any kind of loading bottleneck.
  if (TIFFIsTiled(tiff)) {
    tsize_t tilesize = TIFFTileSize(tiff);
    uint32_t ntiles = TIFFNumberOfTiles(tiff);
    if (ntiles != 1) {
      LOG_ERROR << "Reader doesn't support more than 1 tile per plane";
      return false;
    }
    // assuming ntiles == 1 for all IFDs
    tdata_t buf = _TIFFmalloc(tilesize);

    uint32_t i = planeIndex;

    numBytesRead = TIFFReadEncodedTile(tiff, 0, buf, tilesize);
    if (numBytesRead < 0) {
      LOG_ERROR << "Error reading tiff tile";
      _TIFFfree(buf);
      return false;
    }
    // copy buf into data.
    size_t numBytesCopied = copyDirect(dataPtr, static_cast<uint8_t*>(buf), numBytesRead, dims.bitsPerPixel);

    _TIFFfree(buf);
    // if something went wrong at this level, bail out
    if (numBytesCopied == 0) {
      return false;
    }

  } else {
    // stripped.

    uint32_t i = planeIndex;

    uint32_t planeindexintiff = i;

    // Number of bytes in a decoded scanline
    tsize_t striplength = TIFFStripSize(tiff);
    tdata_t buf = _TIFFmalloc(striplength);

    uint32_t nstrips = TIFFNumberOfStrips(tiff);
    // LOG_DEBUG << nstrips;     // num y rows
    // LOG_DEBUG << striplength; // x width * rows per strip
    for (tstrip_t strip = 0; strip < nstrips; strip++) {
      numBytesRead = TIFFReadEncodedStrip(tiff, strip, buf, striplength);
      if (numBytesRead < 0) {
        LOG_ERROR << "Error reading tiff strip";
        _TIFFfree(buf);
        return false;
      }

      // copy buf into data.
      size_t numBytesCopied = copyDirect(dataPtr, static_cast<uint8_t*>(buf), numBytesRead, dims.bitsPerPixel);

      // if something went wrong at this level, bail out
      if (numBytesCopied == 0) {
        _TIFFfree(buf);
        return false;
      }
      // advance to next strip
      dataPtr += numBytesCopied;
    }
    _TIFFfree(buf);
  }
  return true;
}

VolumeDimensions
FileReaderTIFF::loadDimensions(const std::string& filepath, uint32_t scene)
{
  ScopedTiffReader tiffreader(filepath);
  TIFF* tiff = tiffreader.reader();
  // Loads tiff file
  if (!tiff) {
    return VolumeDimensions();
  }

  VolumeDimensions dims;
  int32_t numScenesInFile = 0;
  bool dims_ok = readTiffDimensions(tiff, filepath, dims, scene);
  if (!dims_ok) {
    return VolumeDimensions();
  }
  return dims;
}

std::shared_ptr<ImageXYZC>
FileReaderTIFF::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  std::string filepath = loadSpec.filepath;
  uint32_t time = loadSpec.time;
  uint32_t scene = loadSpec.scene;
  VolumeDimensions outDims;

  std::shared_ptr<ImageXYZC> emptyimage;

  auto tStart = std::chrono::high_resolution_clock::now();

  // Loads tiff file
  ScopedTiffReader tiffreader(filepath);
  TIFF* tiff = tiffreader.reader();
  if (!tiff) {
    return emptyimage;
  }

  VolumeDimensions dims;
  bool dims_ok = readTiffDimensions(tiff, filepath, dims, scene);
  if (!dims_ok) {
    return emptyimage;
  }

  if (time > (int32_t)(dims.sizeT - 1)) {
    LOG_ERROR << "Time " << time << " exceeds time samples in file: " << dims.sizeT;
    return emptyimage;
  }

  LOG_DEBUG << "Reading " << (TIFFIsTiled(tiff) ? "tiled" : "stripped") << " tiff...";

  uint32_t rowsPerStrip = 0;
  if (TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip)) {
    LOG_DEBUG << "ROWSPERSTRIP: " << rowsPerStrip;
    uint32_t StripsPerImage = ((dims.sizeY + rowsPerStrip - 1) / rowsPerStrip);
    LOG_DEBUG << "Strips per image: " << StripsPerImage;
  }
  uint32_t samplesPerPixel = 0;
  if (TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel)) {
    LOG_DEBUG << "SamplesPerPixel: " << samplesPerPixel;
  }
  if (samplesPerPixel != 1) {
    LOG_WARNING << "" << samplesPerPixel
                << " samples per pixel is not supported in tiff. Attempting to ignore and use 1 sample";
    samplesPerPixel = 1;
    // return emptyimage;
  }

  uint32_t planarConfig = 0;
  if (TIFFGetField(tiff, TIFFTAG_PLANARCONFIG, &planarConfig)) {
    LOG_DEBUG << "PlanarConfig: " << (planarConfig == 1 ? "PLANARCONFIG_CONTIG" : "PLANARCONFIG_SEPARATE");
  }

  uint32_t nch = loadSpec.channels.empty() ? dims.sizeC : loadSpec.channels.size();

  size_t planesize_bytes = (size_t)dims.sizeX * (size_t)dims.sizeY * (ImageXYZC::IN_MEMORY_BPP / 8);
  size_t channelsize_bytes = planesize_bytes * dims.sizeZ;
  uint8_t* data = new uint8_t[channelsize_bytes * nch];
  memset(data, 0, channelsize_bytes * nch);
  // stash it here in case of early exit, it will be deleted
  std::unique_ptr<uint8_t[]> smartPtr(data);

  uint8_t* destptr = data;

  // still assuming 1 sample per pixel (scalar data) here.
  size_t rawPlanesize = (size_t)dims.sizeX * (size_t)dims.sizeY * (size_t)(dims.bitsPerPixel / 8);
  // allocate temp data for one channel
  uint8_t* channelRawMem = new uint8_t[(size_t)dims.sizeZ * rawPlanesize];
  memset(channelRawMem, 0, (size_t)dims.sizeZ * rawPlanesize);

  // stash it here in case of early exit, it will be deleted
  std::unique_ptr<uint8_t[]> smartPtrTemp(channelRawMem);

  // now ready to read channels one by one.
  for (uint32_t channel = 0; channel < nch; ++channel) {
    uint32_t channelToLoad = channel;
    if (!loadSpec.channels.empty()) {
      channelToLoad = loadSpec.channels[channel];
    }

    // read entire channel into its native size
    for (uint32_t slice = 0; slice < dims.sizeZ; ++slice) {
      // Cancellation boundary: one IFD read per plane, so this is a fine-grained
      // place to abandon an unwanted prefetch.
      if (progress.isCancelled()) {
        return emptyimage;
      }
      uint32_t planeIndex = dims.getPlaneIndex(slice, channelToLoad, time);
      destptr = channelRawMem + slice * rawPlanesize;
      if (!readTiffPlane(tiff, planeIndex, dims, destptr)) {
        return emptyimage;
      }
    }

    // convert to our internal format (IN_MEMORY_BPP)
    if (!FileReaderUtil::convertChannelData(data + channel * channelsize_bytes, channelRawMem, dims)) {
      return emptyimage;
    }

    progress.setProgress(channel + 1, nch);
  }

  auto tEnd = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = tEnd - tStart;
  LOG_DEBUG << "TIFF loaded in " << (elapsed.count() * 1000.0) << "ms";

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
}

std::vector<MultiscaleDims>
FileReaderTIFF::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
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
