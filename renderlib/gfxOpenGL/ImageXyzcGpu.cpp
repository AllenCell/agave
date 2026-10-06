#include "ImageXyzcGpu.h"

#include "AppScene.h" // for VolumeDisplay!  REFACTOR
#include "ImageXYZC.h"
#include "Logging.h"
#include "threading.h"

#include "gfxOpenGL/Util.h"

#include <chrono>

static constexpr int LUT_SIZE = 256;

ChannelGpu::~ChannelGpu()
{
  deallocGpu();
  m_VolumeLutGLTexture = 0;
  m_gpuBytes = 0;
  LOG_DEBUG << "ChannelGpu destructor called.";
}

void
ChannelGpu::allocGpu(ImageXYZC* img, int channel)
{
  Channelu16* ch = img->channel(channel);

  // LUT buffer

  m_gpuBytes += static_cast<size_t>(32) / 8 * LUT_SIZE;

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glGenTextures(1, &m_VolumeLutGLTexture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_VolumeLutGLTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, LUT_SIZE, 1);
  // glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, LUT_SIZE, 1, 0, GL_RED, GL_FLOAT, ch->m_lut);

  glBindTexture(GL_TEXTURE_2D, 0);
  check_gl("volume lut texture creation");
}

void
ChannelGpu::deallocGpu()
{
  glDeleteTextures(1, &m_VolumeLutGLTexture);
  m_VolumeLutGLTexture = 0;

  m_gpuBytes = 0;
}

void
ChannelGpu::updateLutGpu(int channel, ImageXYZC* img)
{
  static const int LUT_SIZE = 256;
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, 0);
  check_gl("unbind to update lut texture");

  glBindTexture(GL_TEXTURE_2D, m_VolumeLutGLTexture);
  check_gl("bind lut texture");
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LUT_SIZE, 1, GL_RED, GL_FLOAT, img->channel(channel)->m_lut);
  check_gl("update lut texture");

  glBindTexture(GL_TEXTURE_2D, 0);
  check_gl("unbind lut texture");
}

void
ImageGpu::createVolumeTextureFusedRGBA8(ImageXYZC* img)
{
  m_gpuBytes += (8 + 8 + 8 + 8) / 8 * img->sizeX() * img->sizeY() * img->sizeZ();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glGenTextures(1, &m_VolumeGLTexture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, m_VolumeGLTexture);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, img->sizeX(), img->sizeY(), img->sizeZ());
  glBindTexture(GL_TEXTURE_3D, 0);
  check_gl("fused rgb volume texture creation");
}

void
ImageGpu::createVolumeTexture4x16(ImageXYZC* img)
{
  int N = 4;
  if (img->sizeC() < 4) {
    N = img->sizeC();
  }

  m_gpuBytes += (16 * N) / 8 * img->sizeX() * img->sizeY() * img->sizeZ();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glGenTextures(1, &m_VolumeGLTexture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, m_VolumeGLTexture);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);

  GLenum internalFormat = GL_RGBA16;
  if (img->sizeC() == 3) {
    internalFormat = GL_RGB16;
  } else if (img->sizeC() == 2) {
    internalFormat = GL_RG16;
  } else if (img->sizeC() == 1) {
    internalFormat = GL_R16;
  }
  glTexStorage3D(GL_TEXTURE_3D, 1, internalFormat, img->sizeX(), img->sizeY(), img->sizeZ());
  glBindTexture(GL_TEXTURE_3D, 0);

  glGenTextures(1, &m_ActiveChannelColormaps);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_ActiveChannelColormaps);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, LUT_SIZE, 1, 4);
  // 4 * glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, LUT_SIZE, 1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  check_gl("volume texture creation");
}

void
ImageGpu::updateVolumeData4x16(ImageXYZC* img, int c0, int c1, int c2, int c3)
{
  auto startTime = std::chrono::high_resolution_clock::now();

  size_t N = 4;
  if (img->sizeC() < 4) {
    N = img->sizeC();
  }
  int ch[4] = { c0, c1, c2, c3 };
  // interleaved all channels.
  // up to the first 4.
  size_t xyz = img->sizeX() * img->sizeY() * img->sizeZ();
  uint16_t* v = new uint16_t[xyz * N];

  parallel_for(xyz, [&N, &v, &img, &ch](size_t s, size_t e) {
    for (size_t i = s; i < e; ++i) {
      for (size_t j = 0; j < N; ++j) {
        v[N * (i) + j] = img->channel(ch[j])->m_ptr[(i)];
      }
    }
  });

  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = endTime - startTime;
  LOG_DEBUG << "Prepared interleaved hostmem buffer: " << (elapsed.count() * 1000.0) << "ms";

  startTime = std::chrono::high_resolution_clock::now();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, m_VolumeGLTexture);
  GLenum dataFormat = GL_RGBA;
  if (img->sizeC() == 1) {
    dataFormat = GL_RED;
  } else if (img->sizeC() == 2) {
    dataFormat = GL_RG;
  } else if (img->sizeC() == 3) {
    dataFormat = GL_RGB;
  }
  try {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // do this in chunks for very large data sizes.
    static const size_t GB = static_cast<size_t>(1024) * 1024 * 1024;
    size_t chunkThresholdBytes = 4 * GB;
    // if whole volume is larger than chunk size:
    if (xyz * N * sizeof(uint16_t) > chunkThresholdBytes) {
      // split the operation into chunks.
      // find number of z planes that fit into 1 chunk:
      size_t zPlanes = img->sizeZ();
      size_t planeSizeElements = img->sizeX() * img->sizeY() * N;
      size_t zPlanesPerChunk = chunkThresholdBytes / (planeSizeElements * sizeof(uint16_t));
      size_t numChunks = (zPlanes + zPlanesPerChunk - 1) / zPlanesPerChunk;
      LOG_DEBUG << "Updating volume texture in " << numChunks << " chunks of " << zPlanesPerChunk
                << " z-planes each, total size: " << (xyz * sizeof(uint16_t) * N) << " bytes";
      // loop over all the chunks and call glTexSubImage3D with the right z offset for each
      for (size_t chunk = 0, zoffset = 0; chunk < numChunks; ++chunk) {
        // only the last chunk could have less zplanes.
        size_t chunkSizeZ = std::min(zPlanes - zoffset, zPlanesPerChunk);
        glTexSubImage3D(GL_TEXTURE_3D,
                        0,
                        0,
                        0,
                        zoffset,
                        img->sizeX(),
                        img->sizeY(),
                        chunkSizeZ,
                        dataFormat,
                        GL_UNSIGNED_SHORT,
                        v + zoffset * planeSizeElements);
        zoffset += chunkSizeZ;
      }
    } else {
      glTexSubImage3D(
        GL_TEXTURE_3D, 0, 0, 0, 0, img->sizeX(), img->sizeY(), img->sizeZ(), dataFormat, GL_UNSIGNED_SHORT, v);
    }

  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to update volume texture (" << img->sizeX() << ", " << img->sizeY() << ", " << img->sizeZ()
              << "): " << e.what();
    delete[] v;
    return;
  } catch (...) {
    LOG_ERROR << "Failed to update volume texture (" << img->sizeX() << ", " << img->sizeY() << ", " << img->sizeZ()
              << "): unknown error";
    delete[] v;
    return;
  }
  glBindTexture(GL_TEXTURE_3D, 0);
  check_gl("update volume texture");

  endTime = std::chrono::high_resolution_clock::now();
  elapsed = endTime - startTime;
  LOG_DEBUG << "Copy volume to gpu: " << (xyz * sizeof(uint16_t) * N) << " bytes in " << (elapsed.count() * 1000.0)
            << "ms";

  delete[] v;
}

void
ImageGpu::allocGpuInterleaved(ImageXYZC* img, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
  deallocGpu();

  auto startTime = std::chrono::high_resolution_clock::now();

  createVolumeTexture4x16(img);
  uint32_t numChannels = img->sizeC();
  updateVolumeData4x16(img,
                       std::min(c0, numChannels - 1),
                       std::min(c1, numChannels - 1),
                       std::min(c2, numChannels - 1),
                       std::min(c3, numChannels - 1));

  for (uint32_t i = 0; i < numChannels; ++i) {
    ChannelGpu* c = new ChannelGpu(i);
    c->allocGpu(img, i);
    m_channels.push_back(c);

    m_gpuBytes += c->m_gpuBytes;
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = endTime - startTime;
  LOG_DEBUG << "allocGPUinterleaved: Image to GPU in " << (elapsed.count() * 1000.0) << "ms";
  LOG_DEBUG << "allocGPUinterleaved: GPU bytes: " << m_gpuBytes;
}

void
ImageGpu::deallocGpu()
{
  for (size_t i = 0; i < m_channels.size(); ++i) {
    delete m_channels[i];
  }
  m_channels.clear();

  // needs current gl context.

  check_gl("pre-destroy gl volume texture");
  //  glBindTexture(GL_TEXTURE_3D, 0);
  glDeleteTextures(1, &m_VolumeGLTexture);
  check_gl("destroy gl volume texture");
  LOG_DEBUG << "deallocGPU: GPU bytes: " << m_gpuBytes;
  m_VolumeGLTexture = 0;

  glDeleteTextures(1, &m_ActiveChannelColormaps);
  check_gl("destroy gl colormaps texture");
  m_ActiveChannelColormaps = 0;

  m_gpuBytes = 0;

  glFinish();
}

void
ImageGpu::updateLutGPU(ImageXYZC* img, int c0, int c1, int c2, int c3, const VolumeDisplay& volumeDisplay)
{
  // and write color luts
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_ActiveChannelColormaps);
  // mip level, xy offset, layer index
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                  0,
                  0,
                  0,
                  0,
                  LUT_SIZE,
                  1,
                  1,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  volumeDisplay.m_colormap[c0].m_colormap.data());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                  0,
                  0,
                  0,
                  1,
                  LUT_SIZE,
                  1,
                  1,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  volumeDisplay.m_colormap[c1].m_colormap.data());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                  0,
                  0,
                  0,
                  2,
                  LUT_SIZE,
                  1,
                  1,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  volumeDisplay.m_colormap[c2].m_colormap.data());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                  0,
                  0,
                  0,
                  3,
                  LUT_SIZE,
                  1,
                  1,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  volumeDisplay.m_colormap[c3].m_colormap.data());
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void
ImageGpu::updateLutGpu(int channel, ImageXYZC* img)
{
  m_channels[channel]->updateLutGpu(channel, img);
}

void
ImageGpu::setVolumeTextureFiltering(bool linear)
{
  glBindTexture(GL_TEXTURE_3D, m_VolumeGLTexture);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glBindTexture(GL_TEXTURE_3D, 0);
}

ImageGpu::~ImageGpu()
{
  deallocGpu();
  LOG_DEBUG << "ImageGpu destructor called.";
}
