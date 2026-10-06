#pragma once

#include <glad/glad.h>

#include <vector>

class ImageXYZC;
struct VolumeDisplay;

struct ChannelGpu
{
  ChannelGpu(int index)
    : m_index(index)
    , m_VolumeLutGLTexture(0)
    , m_gpuBytes(0)
  {
  }
  ~ChannelGpu();
  GLuint m_VolumeLutGLTexture = 0;

  int m_index;
  size_t m_gpuBytes = 0;

  void allocGpu(ImageXYZC* img, int channel);
  void deallocGpu();
  void updateLutGpu(int channel, ImageXYZC* img);
};

struct ImageGpu
{
  ImageGpu()
    : m_VolumeGLTexture(0)
    , m_ActiveChannelColormaps(0)
    , m_gpuBytes(0)
  {
  }
  ~ImageGpu();

  std::vector<ChannelGpu*> m_channels;

  GLuint m_VolumeGLTexture = 0;
  GLuint m_ActiveChannelColormaps = 0;

  size_t m_gpuBytes = 0;

  // put first 4 channels into gpu array
  void allocGpuInterleaved(ImageXYZC* img, uint32_t c0 = 0u, uint32_t c1 = 1u, uint32_t c2 = 2u, uint32_t c3 = 3u);

  void deallocGpu();

  void updateLutGpu(int channel, ImageXYZC* img);

  void createVolumeTexture4x16(ImageXYZC* img);
  void createVolumeTextureFusedRGBA8(ImageXYZC* img);

  // similar to allocGpuInterleaved, change which channels are in the gpu volume buffer.
  void updateVolumeData4x16(ImageXYZC* img, int c0, int c1, int c2, int c3);
  void updateLutGPU(ImageXYZC* img, int c0, int c1, int c2, int c3, const VolumeDisplay& volumeDisplay);

  void setVolumeTextureFiltering(bool linear);
};
