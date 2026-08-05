#include "FileReaderTestVolume.h"

#include "ImageXYZC.h"
#include "Logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float CENTER = 127.5f;

struct Displacement
{
  float x;
  float y;
  float z;
};

Displacement
displacementFor(uint32_t channel, uint32_t time)
{
  const float phase = 2.0f * PI * static_cast<float>(time) / static_cast<float>(FileReaderTestVolume::SIZE_T);
  switch (channel) {
    case 0:
      return { 30.0f * std::sin(phase), 18.0f * std::sin(2.0f * phase + 0.4f),
               12.0f * std::sin(phase + 1.1f) };
    case 1:
      return { 18.0f * std::sin(2.0f * phase + 0.7f), 28.0f * std::sin(phase + 1.6f),
               14.0f * std::sin(3.0f * phase) };
    default:
      return { 24.0f * std::sin(phase + 2.2f), 16.0f * std::sin(3.0f * phase + 0.3f),
               20.0f * std::sin(2.0f * phase + 1.4f) };
  }
}

void
prepareVolume(std::vector<uint16_t>& destination)
{
  destination.assign(static_cast<size_t>(FileReaderTestVolume::SIZE_X) * FileReaderTestVolume::SIZE_Y *
                       FileReaderTestVolume::SIZE_Z,
                     0);
}
} // namespace

const std::string FileReaderTestVolume::PATH = "TESTVOLUME";

FileReaderTestVolume::FileReaderTestVolume(const std::string& filepath)
{
  (void)filepath;
  setMaxConcurrentLoads(2);
}

size_t
FileReaderTestVolume::voxelIndex(uint32_t x, uint32_t y, uint32_t z)
{
  return (static_cast<size_t>(z) * SIZE_Y + y) * SIZE_X + x;
}

void
FileReaderTestVolume::generateSphereVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz)
{
  prepareVolume(destination);
  constexpr float radiusSquared = 42.0f * 42.0f;
  for (uint32_t z = 0; z < SIZE_Z; ++z) {
    const float pz = static_cast<float>(z) - CENTER - dz;
    for (uint32_t y = 0; y < SIZE_Y; ++y) {
      const float py = static_cast<float>(y) - CENTER - dy;
      for (uint32_t x = 0; x < SIZE_X; ++x) {
        const float px = static_cast<float>(x) - CENTER - dx;
        if (px * px + py * py + pz * pz <= radiusSquared) {
          destination[voxelIndex(x, y, z)] = FOREGROUND_VALUE;
        }
      }
    }
  }
}

void
FileReaderTestVolume::generateTorusVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz)
{
  prepareVolume(destination);
  constexpr float majorRadius = 48.0f;
  constexpr float minorRadiusSquared = 16.0f * 16.0f;
  for (uint32_t z = 0; z < SIZE_Z; ++z) {
    const float pz = static_cast<float>(z) - CENTER - dz;
    for (uint32_t y = 0; y < SIZE_Y; ++y) {
      const float py = static_cast<float>(y) - CENTER - dy;
      for (uint32_t x = 0; x < SIZE_X; ++x) {
        const float px = static_cast<float>(x) - CENTER - dx;
        const float distanceFromRing = std::sqrt(px * px + py * py) - majorRadius;
        if (distanceFromRing * distanceFromRing + pz * pz <= minorRadiusSquared) {
          destination[voxelIndex(x, y, z)] = FOREGROUND_VALUE;
        }
      }
    }
  }
}

void
FileReaderTestVolume::generateConeVolume(std::vector<uint16_t>& destination, float dx, float dy, float dz)
{
  prepareVolume(destination);
  constexpr float halfHeight = 50.0f;
  constexpr float baseRadius = 40.0f;
  for (uint32_t z = 0; z < SIZE_Z; ++z) {
    const float pz = static_cast<float>(z) - CENTER - dz;
    if (pz < -halfHeight || pz > halfHeight) {
      continue;
    }
    const float radius = baseRadius * (halfHeight - pz) / (2.0f * halfHeight);
    const float radiusSquared = radius * radius;
    for (uint32_t y = 0; y < SIZE_Y; ++y) {
      const float py = static_cast<float>(y) - CENTER - dy;
      for (uint32_t x = 0; x < SIZE_X; ++x) {
        const float px = static_cast<float>(x) - CENTER - dx;
        if (px * px + py * py <= radiusSquared) {
          destination[voxelIndex(x, y, z)] = FOREGROUND_VALUE;
        }
      }
    }
  }
}

VolumeDimensions
FileReaderTestVolume::loadDimensions(const std::string& filepath, uint32_t scene)
{
  (void)filepath;
  if (scene != 0) {
    LOG_ERROR << PATH << " contains only scene 0";
    return {};
  }

  VolumeDimensions dims;
  dims.sizeX = SIZE_X;
  dims.sizeY = SIZE_Y;
  dims.sizeZ = SIZE_Z;
  dims.sizeC = SIZE_C;
  dims.sizeT = SIZE_T;
  dims.bitsPerPixel = ImageXYZC::IN_MEMORY_BPP;
  dims.channelNames = { "Sphere", "Torus", "Cone" };
  dims.spatialUnits = "voxels";
  dims.timeUnit = 1.0f;
  dims.timeUnits = "frame";
  return dims;
}

uint32_t
FileReaderTestVolume::loadNumScenes(const std::string& filepath)
{
  (void)filepath;
  return 1;
}

std::vector<MultiscaleDims>
FileReaderTestVolume::loadMultiscaleDims(const std::string& filepath, uint32_t scene)
{
  if (scene != 0) {
    return {};
  }
  const VolumeDimensions dims = loadDimensions(filepath, scene);
  MultiscaleDims multiscale;
  multiscale.shape = { dims.sizeT, dims.sizeC, dims.sizeZ, dims.sizeY, dims.sizeX };
  multiscale.scale = { dims.timeUnit, 1.0f, dims.physicalSizeZ, dims.physicalSizeY, dims.physicalSizeX };
  multiscale.dimensionOrder = { "T", "C", "Z", "Y", "X" };
  multiscale.dtype = "uint16";
  multiscale.channelNames = dims.channelNames;
  multiscale.spatialUnits = dims.spatialUnits;
  multiscale.timeUnits = dims.timeUnits;
  return { multiscale };
}

std::shared_ptr<ImageXYZC>
FileReaderTestVolume::loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress)
{
  if (loadSpec.scene != 0 || loadSpec.time >= SIZE_T) {
    LOG_ERROR << "Invalid " << PATH << " scene or time";
    return {};
  }

  std::vector<uint32_t> channels = loadSpec.channels;
  if (channels.empty()) {
    channels = { 0, 1, 2 };
  }
  if (std::any_of(channels.begin(), channels.end(), [](uint32_t channel) { return channel >= SIZE_C; })) {
    LOG_ERROR << "Invalid " << PATH << " channel";
    return {};
  }

  const size_t voxelsPerChannel = static_cast<size_t>(SIZE_X) * SIZE_Y * SIZE_Z;
  const size_t totalBytes = voxelsPerChannel * channels.size() * sizeof(uint16_t);
  auto data = std::make_unique<uint8_t[]>(totalBytes);
  std::vector<uint16_t> generated;

  for (size_t outputChannel = 0; outputChannel < channels.size(); ++outputChannel) {
    if (progress.isCancelled()) {
      return {};
    }
    const uint32_t sourceChannel = channels[outputChannel];
    const Displacement displacement = displacementFor(sourceChannel, loadSpec.time);
    switch (sourceChannel) {
      case 0:
        generateSphereVolume(generated, displacement.x, displacement.y, displacement.z);
        break;
      case 1:
        generateTorusVolume(generated, displacement.x, displacement.y, displacement.z);
        break;
      default:
        generateConeVolume(generated, displacement.x, displacement.y, displacement.z);
        break;
    }
    std::memcpy(data.get() + outputChannel * voxelsPerChannel * sizeof(uint16_t), generated.data(),
                voxelsPerChannel * sizeof(uint16_t));
    progress.setProgress(static_cast<uint32_t>(outputChannel + 1), static_cast<uint32_t>(channels.size()));
  }

  auto image = std::make_shared<ImageXYZC>(SIZE_X, SIZE_Y, SIZE_Z, static_cast<uint32_t>(channels.size()),
                                           static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP), data.release());
  const VolumeDimensions dims = loadDimensions(loadSpec.filepath, loadSpec.scene);
  std::vector<std::string> channelNames = dims.getChannelNames(channels);
  image->setChannelNames(channelNames);
  return image;
}
