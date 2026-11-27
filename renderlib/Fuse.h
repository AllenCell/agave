#pragma once

#include "glm.h"

#include <vector>

class ImageXYZC;
struct VolumeDisplay;

// Runs a processing step that applies a color into each channel,
// and then combines the channels into a premultiplied RGBA volume.
class Fuse
{
public:
  // Enabled channels contribute opacity even when their color is black.
  // The caller owns the storage for outRGBAVolume.
  static void fuse(const ImageXYZC* img,
                   const std::vector<glm::vec3>& colorsPerChannel,
                   const VolumeDisplay& volumeDisplay,
                   uint8_t** outRGBAVolume,
                   uint16_t** outGradientVolume);
};
