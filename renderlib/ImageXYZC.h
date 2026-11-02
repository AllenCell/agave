#pragma once

#include "Colormap.h"
#include "Histogram.h"

#include "glm.h"

#include <inttypes.h>
#include <string>
#include <vector>

struct Channelu16
{
  Channelu16(uint32_t x, uint32_t y, uint32_t z, uint16_t* ptr);
  ~Channelu16();

  uint32_t m_x, m_y, m_z;

  uint16_t* m_ptr;

  uint16_t* m_gradientMagnitudePtr;

  Histogram m_histogram;
  float* m_lut;

  uint16_t* generateGradientMagnitudeVolume(float scalex, float scaley, float scalez);

  void generateFromGradientData(const GradientData& gradientData)
  {
    delete[] m_lut;
    m_lut = m_histogram.generateFromGradientData(gradientData);
  }

  void generate_auto2()
  {
    delete[] m_lut;
    m_lut = m_histogram.generate_auto2();
  }
  void generate_auto()
  {
    delete[] m_lut;
    m_lut = m_histogram.generate_auto();
  }
  void generate_bestFit()
  {
    delete[] m_lut;
    m_lut = m_histogram.generate_bestFit();
  }
  void generate_chimerax()
  {
    delete[] m_lut;
    m_lut = m_histogram.initialize_thresholds();
  }

  void generate_equalized()
  {
    delete[] m_lut;
    m_lut = m_histogram.generate_equalized();
  }

  void debugprint();
  void debugColormap();
  void debugData();

  std::string m_name;
};

class ImageXYZC
{
public:
  // how many channels to enable on first load by default
  static constexpr int FIRST_N_CHANNELS = 1;

  static constexpr size_t IN_MEMORY_BPP = 16;
  ImageXYZC(uint32_t x,
            uint32_t y,
            uint32_t z,
            uint32_t c,
            uint32_t bpp,
            uint8_t* data = nullptr,
            float sx = 1.0,
            float sy = 1.0,
            float sz = 1.0,
            std::string spatialUnits = "units");
  virtual ~ImageXYZC();

  void setPhysicalSize(float x, float y, float z);

  // +1 means do not flip, -1 means flip
  void setVolumeAxesFlipped(int x, int y, int z);

  size_t sizeX() const;
  size_t sizeY() const;
  size_t sizeZ() const;
  size_t maxPixelDimension() const;

  // should always return positive values
  float physicalSizeX() const;
  float physicalSizeY() const;
  float physicalSizeZ() const;

  std::string spatialUnits() const;

  glm::vec3 getNormalizedDimensions() const;

  glm::vec3 getPhysicalDimensions() const;

  // +1 means do not flip, -1 means flip
  glm::ivec3 getVolumeAxesFlipped() const;

  size_t sizeC() const;

  size_t sizeOfElement() const;
  size_t sizeOfPlane() const;
  size_t sizeOfChannel() const;
  size_t size() const;

  uint8_t* ptr(size_t channel = 0, size_t z = 0) const;
  Channelu16* channel(uint32_t channel) const;

  void setChannelNames(std::vector<std::string>& channelNames);

private:
  uint32_t m_x, m_y, m_z, m_c, m_bpp;
  uint8_t* m_data;
  float m_scaleX, m_scaleY, m_scaleZ;
  glm::ivec3 m_flipped;
  std::string m_spatialUnits;
  std::vector<Channelu16*> m_channels;
};
