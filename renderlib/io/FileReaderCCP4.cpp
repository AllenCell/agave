#include "FileReaderCCP4.h"

#include "BoundingBox.h"
#include "ConvertChannelData.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "VolumeDimensions.h"

#include <tiff.h>
#include <tiffio.h>

#include <chrono>
#include <fstream>
#include <map>
#include <set>

// see https://www.ccpem.ac.uk/mrc_format/mrc2014.php

// 1      NC              # of Columns    (fastest changing in map)
//  2      NR              # of Rows
//  3      NS              # of Sections   (slowest changing in map)
//  4      MODE            Data type
//                           0 = envelope stored as signed bytes (from
//                               -128 lowest to 127 highest)
//                           1 = Image     stored as Integer*2
//                           2 = Image     stored as Reals
//                           3 = Transform stored as Complex Integer*2
//                           4 = Transform stored as Complex Reals
//                           5 == 0

//                           Note: Mode 2 is the normal mode used in
//                                 the CCP4 programs. Other modes than 2 and 0
//                                 may NOT WORK

//  5      NCSTART         Number of first COLUMN  in map
//  6      NRSTART         Number of first ROW     in map
//  7      NSSTART         Number of first SECTION in map
//  8      NX              Number of intervals along X
//  9      NY              Number of intervals along Y
// 10      NZ              Number of intervals along Z
// 11      X length        Cell Dimensions (Angstroms)
// 12      Y length                     "
// 13      Z length                     "
// 14      Alpha           Cell Angles     (Degrees)
// 15      Beta                         "
// 16      Gamma                        "
// 17      MAPC            Which axis corresponds to Cols.  (1,2,3 for X,Y,Z)
// 18      MAPR            Which axis corresponds to Rows   (1,2,3 for X,Y,Z)
// 19      MAPS            Which axis corresponds to Sects. (1,2,3 for X,Y,Z)
// 20      AMIN            Minimum density value
// 21      AMAX            Maximum density value
// 22      AMEAN           Mean    density value    (Average)
// 23      ISPG            Space group number
// 24      NSYMBT          Number of bytes used for storing symmetry operators
// 25      LSKFLG          Flag for skew transformation, =0 none, =1 if foll
// 26-34   SKWMAT          Skew matrix S (in order S11, S12, S13, S21 etc) if
//                         LSKFLG .ne. 0.
// 35-37   SKWTRN          Skew translation t if LSKFLG .ne. 0.
//                         Skew transformation is from standard orthogonal
//                         coordinate frame (as used for atoms) to orthogonal
//                         map frame, as

//                                 Xo(map) = S * (Xo(atoms) - t)

// 38      future use       (some of these are used by the MSUBSX routines
//  .          "              in MAPBRICK, MAPCONT and FRODO)
//  .          "   (all set to zero by default)
//  .          "
// 52          "

// 53    MAP             Character string 'MAP ' to identify file type
// 54    MACHST          Machine stamp indicating the machine type
//                         which wrote file
// 55      ARMS            Rms deviation of map from mean density
// 56      NLABL           Number of labels being used
// 57-256  LABEL(20,10)    10  80 character text labels (ie. A4 format)

static const size_t CCP4_HEADER_SIZE = 256 * 4;
static const size_t CCP4_NSYMBT_OFFSET = 23 * 4;

FileReaderCCP4::FileReaderCCP4(const std::string& filepath) {}

FileReaderCCP4::~FileReaderCCP4() {}

uint32_t
FileReaderCCP4::loadNumScenes(const std::string& filepath)
{
  return 1;
}

size_t
getDataOffset(const std::string filepath)
{
  std::ifstream myFile(filepath, std::ios::in | std::ios::binary);
  myFile.seekg(CCP4_NSYMBT_OFFSET);
  uint32_t numSymBytes = 0;
  myFile.read((char*)&numSymBytes, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return CCP4_HEADER_SIZE;
  }
  return numSymBytes + CCP4_HEADER_SIZE;
}

bool
readCCP4Dimensions(const std::string filepath, VolumeDimensions& dims, uint32_t scene)
{
  std::ifstream myFile(filepath, std::ios::in | std::ios::binary);
  if (!myFile) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // first 4 bytes
  myFile.read((char*)&dims.sizeX, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  myFile.read((char*)&dims.sizeY, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  myFile.read((char*)&dims.sizeZ, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  uint32_t mode = 0;
  myFile.read((char*)&mode, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // 0 = envelope stored as signed bytes (from
  //     -128 lowest to 127 highest)
  // 1 = Image     stored as Integer*2 (16-bit signed integer)
  // 2 = Image     stored as Reals (32-bit float)
  // 3 = Transform stored as Complex Integer*2 (complex 16-bit int)
  // 4 = Transform stored as Complex Reals (complex 32-bit float)
  // 5 == 0
  // 6 == 16-bit unsigned int
  // 12 == 16-bit float
  // 101 == 4-bit data packed two per byte
  // SAMPLEFORMAT_UINT = 1;
  // SAMPLEFORMAT_INT = 2;
  // SAMPLEFORMAT_IEEEFP = 3;
  switch (mode) {
    case 0:
    case 5:
      dims.bitsPerPixel = 8;
      dims.sampleFormat = 2;
      break;
    case 1:
      dims.bitsPerPixel = 16;
      dims.sampleFormat = 2;
      break;
    case 2:
      dims.bitsPerPixel = 32;
      dims.sampleFormat = 3;
      break;
    case 3:
      dims.bitsPerPixel = 32;
      dims.sampleFormat = 2;
      break;
    case 4:
      dims.bitsPerPixel = 64;
      dims.sampleFormat = 3;
      break;
    case 6:
      dims.bitsPerPixel = 16;
      dims.sampleFormat = 1;
    case 12:
      dims.bitsPerPixel = 16;
      dims.sampleFormat = 3;
    default:
      LOG_ERROR << "Bad file read from " << filepath;
      return false;
  }

  uint32_t dummy;
  // NCSTART         Number of first COLUMN  in map
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // NRSTART         Number of first ROW     in map
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // NSSTART         Number of first SECTION in map
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // NX              Number of intervals along X
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // NY              Number of intervals along Y
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // NZ              Number of intervals along Z
  myFile.read((char*)&dummy, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }

  // these values are in Angstroms
  // CELLA.x           Cell Dimensions (Angstroms)
  dims.physicalSizeX = 1.0f;
  myFile.read((char*)&dims.physicalSizeX, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // CELLA.y           Cell Dimensions (Angstroms)
  dims.physicalSizeY = 1.0f;
  myFile.read((char*)&dims.physicalSizeY, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }
  // CELLA.z           Cell Dimensions (Angstroms)
  dims.physicalSizeZ = 1.0f;
  myFile.read((char*)&dims.physicalSizeZ, 4);
  if (!myFile || myFile.gcount() != 4) {
    LOG_ERROR << "Bad file read from " << filepath;
    return false;
  }

  dims.spatialUnits = "Angstroms";
  dims.sizeC = 1;
  dims.sizeT = 1;
  dims.dimensionOrder = "XYZCT";
  dims.channelNames = { "C0" };

  dims.log();

  return dims.validate();
}

// return number of bytes copied to dest
static size_t
copyDirect(uint8_t* dest, const uint8_t* src, size_t numBytes, int srcBitsPerPixel)
{
  memcpy(dest, src, numBytes);
  return numBytes;
}

// DANGER: assumes dataPtr has enough space allocated!!!!
bool
readCCP4Plane(std::ifstream& myFile, size_t offset, size_t numBytes, const VolumeDimensions& dims, uint8_t* dataPtr)
{
  myFile.seekg(offset);
  myFile.read((char*)dataPtr, numBytes);
  if (!myFile || myFile.gcount() != numBytes) {
    LOG_ERROR << "Failed to read raw plane from file";
    return false;
  }
  return true;
}

VolumeDimensions
FileReaderCCP4::loadDimensions(const std::string& filepath, uint32_t scene)
{
  VolumeDimensions dims;
  bool dims_ok = readCCP4Dimensions(filepath, dims, scene);
  if (!dims_ok) {
    return VolumeDimensions();
  }
  return dims;
}

std::shared_ptr<ImageXYZC>
FileReaderCCP4::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  std::string filepath = loadSpec.filepath;
  uint32_t scene = loadSpec.scene;
  uint32_t time = loadSpec.time;
  VolumeDimensions outDims;

  std::shared_ptr<ImageXYZC> emptyimage;

  auto tStart = std::chrono::high_resolution_clock::now();

  VolumeDimensions dims;
  bool dims_ok = readCCP4Dimensions(filepath, dims, scene);
  if (!dims_ok) {
    return emptyimage;
  }

  uint32_t nch = loadSpec.channels.empty() ? dims.sizeC : loadSpec.channels.size();

  size_t dataOffset = getDataOffset(filepath);

  if (scene > 0) {
    LOG_WARNING << "Multiscene CCP4 not supported. Using scene 0";
    scene = 0;
  }
  if (time > (int32_t)(dims.sizeT - 1)) {
    LOG_ERROR << "Time " << time << " exceeds time samples in file: " << dims.sizeT;
    return emptyimage;
  }

  size_t planesize_bytes = (size_t)dims.sizeX * (size_t)dims.sizeY * (size_t)(ImageXYZC::IN_MEMORY_BPP / 8);
  size_t channelsize_bytes = planesize_bytes * (size_t)dims.sizeZ;
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
  std::ifstream myFile(filepath, std::ios::in | std::ios::binary);
  for (uint32_t channel = 0; channel < nch; ++channel) {
    uint32_t channelToLoad = channel;
    if (!loadSpec.channels.empty()) {
      channelToLoad = loadSpec.channels[channel];
    }

    // read entire channel into its native size
    for (uint32_t slice = 0; slice < dims.sizeZ; ++slice) {
      uint32_t planeIndex = dims.getPlaneIndex(slice, channelToLoad, time);
      destptr = channelRawMem + slice * rawPlanesize;
      if (!readCCP4Plane(myFile, dataOffset + rawPlanesize * planeIndex, rawPlanesize, dims, destptr)) {
        return emptyimage;
      }
    }

    // convert to our internal format (IN_MEMORY_BPP)
    if (!FileReaderUtil::convertChannelData(data + channel * channelsize_bytes, channelRawMem, dims)) {
      return emptyimage;
    }
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
FileReaderCCP4::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
{
  std::vector<MultiscaleDims> dims;
  VolumeDimensions vdims;
  bool dims_ok = readCCP4Dimensions(filepath, vdims, scene);
  if (!dims_ok) {
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
