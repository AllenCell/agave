#include "ConvertChannelData.h"

#include "ImageXYZC.h"
#include "Logging.h"
#include "VolumeDimensions.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace FileReaderUtil {
// convert pixels
// this assumes tight packing of pixels in both buf(source) and dataptr(dest)
// assumes dest is of format IN_MEMORY_BPP
// return 1 for successful conversion, 0 on failure (e.g. unacceptable srcBitsPerPixel)
size_t
convertChannelData(uint8_t* dest, const uint8_t* src, const VolumeDimensions& dims)
{
  // how many pixels in this channel:
  size_t numPixels = (size_t)dims.sizeX * (size_t)dims.sizeY * (size_t)dims.sizeZ;
  size_t srcBitsPerPixel = dims.bitsPerPixel;

  // dest bits per pixel is IN_MEMORY_BPP which is currently 16, or 2 bytes
  if (ImageXYZC::IN_MEMORY_BPP == srcBitsPerPixel) {
    memcpy(dest, src, numPixels * (srcBitsPerPixel / 8));
    return 1;
  } else if (srcBitsPerPixel == 8) {
    uint16_t* dataptr16 = reinterpret_cast<uint16_t*>(dest);
    for (size_t b = 0; b < numPixels; ++b) {
      *dataptr16 = (uint16_t)src[b];
      dataptr16++;
    }
    return 1;
  } else if (srcBitsPerPixel == 32) {
    // assumes 32-bit floating point (not int or uint)
    uint16_t* dataptr16 = reinterpret_cast<uint16_t*>(dest);
    const float* src32 = reinterpret_cast<const float*>(src);
    // compute min and max; and then rescale values to fill dynamic range.
    float lowest = FLT_MAX;
    float highest = -FLT_MAX;
    float f;
    for (size_t b = 0; b < numPixels; ++b) {
      f = src32[b];
      if (!std::isfinite(f)) {
        LOG_ERROR << "Float volume data contains a non-finite value";
        return 0;
      }
      if (f < lowest) {
        lowest = f;
      }
      if (f > highest) {
        highest = f;
      }
    }
    if (highest == lowest) {
      memset(dest, 0, numPixels * (ImageXYZC::IN_MEMORY_BPP / 8));
      return 1;
    }
    for (size_t b = 0; b < numPixels; ++b) {
      *dataptr16 = (uint16_t)((src32[b] - lowest) / (highest - lowest) * 65535.0);
      dataptr16++;
    }
    return 1;
  } else {
    LOG_ERROR << "Unexpected source pixel size " << srcBitsPerPixel << " bits";
    return 0;
  }
}

} // namespace FileReaderUtil
