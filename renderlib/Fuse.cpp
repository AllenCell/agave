#include "Fuse.h"

#include "AppScene.h" // for VolumeDisplay!  REFACTOR
#include "GradientData.h"
#include "ImageXYZC.h"

#include "threading.h"

#include <algorithm>
#include <cstring>

// fuse: fill volume of color data, plus volume of gradients
// n channels with n colors: use "max" or "avg"
// n channels with gradients: use "max" or "avg"
void
Fuse::fuse(const ImageXYZC* img,
           const std::vector<glm::vec3>& colorsPerChannel,
           const VolumeDisplay& volumeDisplay,
           uint8_t** outRGBAVolume,
           uint16_t** outGradientVolume)
{
  // todo: this can easily be a cuda kernel that loops over channels and does a max operation, if it has the full volume
  // data in gpu mem.

  // create and zero
  uint8_t* rgbaVolume = *outRGBAVolume;
  memset(rgbaVolume, 0, 4 * img->sizeX() * img->sizeY() * img->sizeZ() * sizeof(uint8_t));

  const bool FUSE_THREADED = true;

  parallel_for(
    img->sizeX() * img->sizeY() * img->sizeZ(),
    [&img, &colorsPerChannel, &volumeDisplay, &rgbaVolume](size_t s, size_t e) {
      float value = 0;
      uint16_t rawvalue = 0;
      float normalizedvalue = 0;
      float lutnormalizedvalue = 0;
      float r = 0, g = 0, b = 0;
      float cr = 0, cg = 0, cb = 0;
      uint8_t ar = 0, ag = 0, ab = 0;
      bool isLabels;

      size_t ncolors = colorsPerChannel.size();
      size_t nch = std::min((size_t)img->sizeC(), ncolors);

      for (uint32_t i = 0; i < nch; ++i) {
        glm::vec3 c = colorsPerChannel[i];
        if (!volumeDisplay.m_enabled[i]) {
          continue;
        }
        r = c.x; // 0..1
        g = c.y;
        b = c.z;
        uint16_t* channeldata = reinterpret_cast<uint16_t*>(img->ptr(i));

        // array of 256 floats
        float* lut = img->channel(i)->m_lut;
        float chmax = (float)img->channel(i)->m_histogram.getDataMax();
        float chmin = (float)img->channel(i)->m_histogram.getDataMin();
        // lut = luts[idx][c.enhancement];

        isLabels = volumeDisplay.m_labels[i] > 0 ? true : false;
        const uint8_t* colormap = volumeDisplay.m_colormap[i].m_colormap.data();
        //   get a min/max from the gradient data if possible
        uint16_t imin16 = 0;
        uint16_t imax16 = 0;
        bool hasMinMax = volumeDisplay.m_gradientData[i].getMinMax(img->channel(i)->m_histogram, &imin16, &imax16);
        uint16_t lutmin = hasMinMax ? imin16 : chmin;
        uint16_t lutmax = hasMinMax ? imax16 : chmax;

        // channel data cx is scalar so loop from s to e
        // Fused data is premultiplied RGBA. Opacity is independent of color.
        for (size_t cx = s, fx = s * 4; cx < e; cx++, fx += 4) {
          rawvalue = channeldata[cx];
          normalizedvalue = (float)(rawvalue - chmin) / (float)(chmax - chmin);
          value = lut[(int)(normalizedvalue * 255.0 + 0.5)]; // 0..1

          // apply colormap
          // if not labels then do lookup with normalized value
          if (isLabels) {
            cr = colormap ? r * (float)colormap[(rawvalue % 256) * 4 + 0] / 255.0f : r;
            cg = colormap ? g * (float)colormap[(rawvalue % 256) * 4 + 1] / 255.0f : g;
            cb = colormap ? b * (float)colormap[(rawvalue % 256) * 4 + 2] / 255.0f : b;
          } else {
            lutnormalizedvalue = (float)(rawvalue - lutmin) / (float)(lutmax - lutmin);
            if (lutnormalizedvalue < 0.0f) {
              lutnormalizedvalue = 0.0f;
            }
            if (lutnormalizedvalue > 1.0f) {
              lutnormalizedvalue = 1.0f;
            }
            cr = colormap ? r * (float)colormap[(int)(lutnormalizedvalue * 255.0) * 4 + 0] / 255.0f : r;
            cg = colormap ? g * (float)colormap[(int)(lutnormalizedvalue * 255.0) * 4 + 1] / 255.0f : g;
            cb = colormap ? b * (float)colormap[(int)(lutnormalizedvalue * 255.0) * 4 + 2] / 255.0f : b;
          }
          //  what if rgb*value > 1?
          const float alpha = std::clamp(value * volumeDisplay.m_opacity[i], 0.0f, 1.0f);
          ar = rgbaVolume[fx + 0];
          rgbaVolume[fx + 0] = std::max(ar, static_cast<uint8_t>(std::clamp(cr * alpha, 0.0f, 1.0f) * 255));
          ag = rgbaVolume[fx + 1];
          rgbaVolume[fx + 1] = std::max(ag, static_cast<uint8_t>(std::clamp(cg * alpha, 0.0f, 1.0f) * 255));
          ab = rgbaVolume[fx + 2];
          rgbaVolume[fx + 2] = std::max(ab, static_cast<uint8_t>(std::clamp(cb * alpha, 0.0f, 1.0f) * 255));
          rgbaVolume[fx + 3] = std::max(rgbaVolume[fx + 3], static_cast<uint8_t>(alpha * 255));
        }
      }
    },
    FUSE_THREADED);
}
