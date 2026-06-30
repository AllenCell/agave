#include "VolumeTextureVk.h"

#include "AppScene.h"
#include "Fuse.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "threading.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace gfxvulkan {

namespace {

constexpr uint32_t kTransferSize = 256;
constexpr uint32_t kTransferLayers = 4;
constexpr float kInvUint16Max = 1.0f / 65535.0f;

uint8_t
toU8(float value)
{
  return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, value * 255.0f)));
}

} // namespace

VolumeTextureVk::VolumeTextureVk(Backend& backend)
  : m_backend(backend)
{
}

VolumeTextureVk::~VolumeTextureVk()
{
  release();
}

bool
VolumeTextureVk::upload(const Scene& scene, VolumeTextureMode mode, bool linearFiltering)
{
  release();
  m_mode = mode;

  if (!scene.m_volume) {
    return false;
  }

  switch (mode) {
    case VolumeTextureMode::RawRgba16:
      return uploadRaw(scene, linearFiltering);
    case VolumeTextureMode::FusedRgba8:
    default:
      return uploadFused(scene, linearFiltering);
  }
}

void
VolumeTextureVk::release()
{
  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_transferSampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, m_transferSampler, nullptr);
    m_transferSampler = VK_NULL_HANDLE;
  }
  if (m_transferView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, m_transferView, nullptr);
    m_transferView = VK_NULL_HANDLE;
  }
  if (m_transferImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, m_transferImage, nullptr);
    m_transferImage = VK_NULL_HANDLE;
  }
  if (m_transferMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_transferMemory, nullptr);
    m_transferMemory = VK_NULL_HANDLE;
  }

  if (m_volumeSampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, m_volumeSampler, nullptr);
    m_volumeSampler = VK_NULL_HANDLE;
  }
  if (m_volumeView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, m_volumeView, nullptr);
    m_volumeView = VK_NULL_HANDLE;
  }
  if (m_volumeImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, m_volumeImage, nullptr);
    m_volumeImage = VK_NULL_HANDLE;
  }
  if (m_volumeMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_volumeMemory, nullptr);
    m_volumeMemory = VK_NULL_HANDLE;
  }

  m_dimensions = glm::ivec3(0);
  m_lutMin = glm::vec4(0.0f);
  m_lutMax = glm::vec4(1.0f);
  m_gpuBytes = 0;
}

bool
VolumeTextureVk::createSampler(bool linearFiltering, VkSampler& sampler)
{
  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.minFilter = linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  VkResult result = vkCreateSampler(m_backend.logicalDevice(), &samplerInfo, nullptr, &sampler);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateSampler failed with VkResult " << result;
    return false;
  }
  return true;
}

bool
VolumeTextureVk::uploadVolumeBytes(const void* data,
                                   size_t byteCount,
                                   VkFormat format,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   bool linearFiltering)
{
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  if (!createBuffer(m_backend,
                    static_cast<VkDeviceSize>(byteCount),
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer,
                    stagingMemory)) {
    return false;
  }

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), stagingMemory, 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped);
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(m_backend.logicalDevice(), stagingMemory);

  const bool ok =
    createImage(m_backend,
                width,
                height,
                depth,
                1,
                format,
                VK_IMAGE_TYPE_3D,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                m_volumeImage,
                m_volumeMemory) &&
    createImageView(m_backend, m_volumeImage, format, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_ASPECT_COLOR_BIT, 1, m_volumeView) &&
    createSampler(linearFiltering, m_volumeSampler);

  if (ok) {
    transitionImageLayout(m_backend,
                          m_volumeImage,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(m_backend, stagingBuffer, m_volumeImage, width, height, depth);
    transitionImageLayout(m_backend,
                          m_volumeImage,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_dimensions = glm::ivec3(static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth));
    m_gpuBytes += byteCount;
  }

  vkDestroyBuffer(m_backend.logicalDevice(), stagingBuffer, nullptr);
  vkFreeMemory(m_backend.logicalDevice(), stagingMemory, nullptr);
  return ok;
}

bool
VolumeTextureVk::uploadTransferBytes(const void* data, size_t byteCount)
{
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  if (!createBuffer(m_backend,
                    static_cast<VkDeviceSize>(byteCount),
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer,
                    stagingMemory)) {
    return false;
  }

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), stagingMemory, 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped);
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(m_backend.logicalDevice(), stagingMemory);

  const bool ok =
    createImage(m_backend,
                kTransferSize,
                1,
                1,
                kTransferLayers,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                m_transferImage,
                m_transferMemory) &&
    createImageView(m_backend,
                    m_transferImage,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    kTransferLayers,
                    m_transferView) &&
    createSampler(false, m_transferSampler);

  if (ok) {
    transitionImageLayout(m_backend,
                          m_transferImage,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          kTransferLayers);
    copyBufferToImage(m_backend, stagingBuffer, m_transferImage, kTransferSize, 1, 1, kTransferLayers);
    transitionImageLayout(m_backend,
                          m_transferImage,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          kTransferLayers);
    m_gpuBytes += byteCount;
  }

  vkDestroyBuffer(m_backend.logicalDevice(), stagingBuffer, nullptr);
  vkFreeMemory(m_backend.logicalDevice(), stagingMemory, nullptr);
  return ok;
}

std::array<uint32_t, 4>
VolumeTextureVk::activeChannels(const Scene& scene) const
{
  std::array<uint32_t, 4> channels{ 0, 0, 0, 0 };
  if (!scene.m_volume || scene.m_volume->sizeC() == 0) {
    return channels;
  }

  uint32_t writeIndex = 0;
  for (uint32_t c = 0; c < scene.m_volume->sizeC() && writeIndex < channels.size(); ++c) {
    if (c < MAX_CPU_CHANNELS && scene.m_material.m_enabled[c]) {
      channels[writeIndex++] = c;
    }
  }

  while (writeIndex < channels.size()) {
    channels[writeIndex] = channels[writeIndex == 0 ? 0 : writeIndex - 1];
    ++writeIndex;
  }

  return channels;
}

bool
VolumeTextureVk::uploadFused(const Scene& scene, bool linearFiltering)
{
  ImageXYZC* img = scene.m_volume.get();
  const size_t voxelCount = img->sizeX() * img->sizeY() * img->sizeZ();

  std::vector<glm::vec3> colors;
  colors.reserve(MAX_CPU_CHANNELS);
  for (int i = 0; i < static_cast<int>(MAX_CPU_CHANNELS); ++i) {
    if (scene.m_material.m_enabled[i]) {
      colors.push_back(glm::vec3(scene.m_material.m_diffuse[i * 3],
                                 scene.m_material.m_diffuse[i * 3 + 1],
                                 scene.m_material.m_diffuse[i * 3 + 2]) *
                       scene.m_material.m_opacity[i]);
    } else {
      colors.push_back(glm::vec3(0.0f));
    }
  }

  std::vector<uint8_t> rgb(voxelCount * 3);
  uint8_t* rgbPtr = rgb.data();
  Fuse::fuse(img, colors, scene.m_material, &rgbPtr, nullptr);

  std::vector<uint8_t> rgba(voxelCount * 4);
  parallel_for(voxelCount, [&rgb, &rgba](size_t s, size_t e) {
    for (size_t i = s; i < e; ++i) {
      rgba[i * 4 + 0] = rgb[i * 3 + 0];
      rgba[i * 4 + 1] = rgb[i * 3 + 1];
      rgba[i * 4 + 2] = rgb[i * 3 + 2];
      rgba[i * 4 + 3] = 255;
    }
  });

  std::array<uint8_t, kTransferSize * kTransferLayers * 4> transfer = {};
  for (uint32_t layer = 0; layer < kTransferLayers; ++layer) {
    for (uint32_t i = 0; i < kTransferSize; ++i) {
      const size_t offset = (layer * kTransferSize + i) * 4;
      transfer[offset + 0] = static_cast<uint8_t>(i);
      transfer[offset + 1] = static_cast<uint8_t>(i);
      transfer[offset + 2] = static_cast<uint8_t>(i);
      transfer[offset + 3] = static_cast<uint8_t>(i);
    }
  }

  return uploadVolumeBytes(rgba.data(),
                           rgba.size(),
                           VK_FORMAT_R8G8B8A8_UNORM,
                           static_cast<uint32_t>(img->sizeX()),
                           static_cast<uint32_t>(img->sizeY()),
                           static_cast<uint32_t>(img->sizeZ()),
                           linearFiltering) &&
         uploadTransferBytes(transfer.data(), transfer.size());
}

bool
VolumeTextureVk::uploadRaw(const Scene& scene, bool linearFiltering)
{
  ImageXYZC* img = scene.m_volume.get();
  const size_t voxelCount = img->sizeX() * img->sizeY() * img->sizeZ();
  const std::array<uint32_t, 4> channels = activeChannels(scene);

  std::vector<uint16_t> rgba16(voxelCount * 4);
  parallel_for(voxelCount, [&rgba16, &img, &channels](size_t s, size_t e) {
    for (size_t i = s; i < e; ++i) {
      rgba16[i * 4 + 0] = img->channel(channels[0])->m_ptr[i];
      rgba16[i * 4 + 1] = img->channel(channels[1])->m_ptr[i];
      rgba16[i * 4 + 2] = img->channel(channels[2])->m_ptr[i];
      rgba16[i * 4 + 3] = img->channel(channels[3])->m_ptr[i];
    }
  });

  std::array<uint8_t, kTransferSize * kTransferLayers * 4> transfer = {};
  m_lutMin = glm::vec4(0.0f);
  m_lutMax = glm::vec4(1.0f);

  for (uint32_t layer = 0; layer < kTransferLayers; ++layer) {
    const uint32_t channel = channels[layer];
    Channelu16* ch = img->channel(channel);
    const bool enabled = channel < MAX_CPU_CHANNELS && scene.m_material.m_enabled[channel];
    uint16_t lutMin16 = static_cast<uint16_t>(ch->m_histogram.getDataMin());
    uint16_t lutMax16 = static_cast<uint16_t>(ch->m_histogram.getDataMax());
    uint16_t gradientMin = 0;
    uint16_t gradientMax = 0;
    if (channel < MAX_CPU_CHANNELS &&
        scene.m_material.m_gradientData[channel].getMinMax(ch->m_histogram, &gradientMin, &gradientMax)) {
      lutMin16 = gradientMin;
      lutMax16 = gradientMax;
    }

    m_lutMin[layer] = static_cast<float>(lutMin16) * kInvUint16Max;
    m_lutMax[layer] = std::max(static_cast<float>(lutMax16) * kInvUint16Max, m_lutMin[layer] + kInvUint16Max);

    const uint8_t* colormap = channel < MAX_CPU_CHANNELS ? scene.m_material.m_colormap[channel].m_colormap.data()
                                                         : nullptr;
    const glm::vec3 diffuse = channel < MAX_CPU_CHANNELS
                                ? glm::vec3(scene.m_material.m_diffuse[channel * 3],
                                            scene.m_material.m_diffuse[channel * 3 + 1],
                                            scene.m_material.m_diffuse[channel * 3 + 2])
                                : glm::vec3(1.0f);
    const float opacity = channel < MAX_CPU_CHANNELS ? scene.m_material.m_opacity[channel] : 1.0f;

    for (uint32_t i = 0; i < kTransferSize; ++i) {
      const float lutValue = ch->m_lut ? ch->m_lut[i] : static_cast<float>(i) / 255.0f;
      const size_t colorIndex = i * 4;
      const glm::vec3 ramp = colormap ? glm::vec3(colormap[colorIndex + 0],
                                                  colormap[colorIndex + 1],
                                                  colormap[colorIndex + 2]) /
                                          255.0f
                                      : glm::vec3(1.0f);
      const glm::vec3 color = enabled ? diffuse * ramp * lutValue : glm::vec3(0.0f);
      const float alpha = enabled ? opacity * lutValue : 0.0f;
      const size_t offset = (layer * kTransferSize + i) * 4;
      transfer[offset + 0] = toU8(color.r);
      transfer[offset + 1] = toU8(color.g);
      transfer[offset + 2] = toU8(color.b);
      transfer[offset + 3] = toU8(alpha);
    }
  }

  return uploadVolumeBytes(rgba16.data(),
                           rgba16.size() * sizeof(uint16_t),
                           VK_FORMAT_R16G16B16A16_UNORM,
                           static_cast<uint32_t>(img->sizeX()),
                           static_cast<uint32_t>(img->sizeY()),
                           static_cast<uint32_t>(img->sizeZ()),
                           linearFiltering) &&
         uploadTransferBytes(transfer.data(), transfer.size());
}

} // namespace gfxvulkan
