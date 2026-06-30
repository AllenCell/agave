#pragma once

#include "glm.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

class Scene;

namespace gfxvulkan {

class Backend;

enum class VolumeTextureMode
{
  FusedRgba8,
  RawRgba16,
};

class VolumeTextureVk
{
public:
  explicit VolumeTextureVk(Backend& backend);
  ~VolumeTextureVk();

  bool upload(const Scene& scene, VolumeTextureMode mode, bool linearFiltering);
  void release();

  bool valid() const { return m_volumeImage != VK_NULL_HANDLE && m_volumeView != VK_NULL_HANDLE; }
  VkImageView volumeView() const { return m_volumeView; }
  VkSampler volumeSampler() const { return m_volumeSampler; }
  VkImageView transferView() const { return m_transferView; }
  VkSampler transferSampler() const { return m_transferSampler; }

  glm::vec4 lutMin() const { return m_lutMin; }
  glm::vec4 lutMax() const { return m_lutMax; }
  glm::ivec3 dimensions() const { return m_dimensions; }
  size_t gpuBytes() const { return m_gpuBytes; }
  VolumeTextureMode mode() const { return m_mode; }

private:
  bool uploadVolumeBytes(const void* data,
                         size_t byteCount,
                         VkFormat format,
                         uint32_t width,
                         uint32_t height,
                         uint32_t depth,
                         bool linearFiltering);
  bool uploadTransferBytes(const void* data, size_t byteCount);
  bool createSampler(bool linearFiltering, VkSampler& sampler);
  bool uploadFused(const Scene& scene, bool linearFiltering);
  bool uploadRaw(const Scene& scene, bool linearFiltering);
  std::array<uint32_t, 4> activeChannels(const Scene& scene) const;

  Backend& m_backend;
  VolumeTextureMode m_mode = VolumeTextureMode::FusedRgba8;
  glm::ivec3 m_dimensions = glm::ivec3(0);
  glm::vec4 m_lutMin = glm::vec4(0.0f);
  glm::vec4 m_lutMax = glm::vec4(1.0f);
  size_t m_gpuBytes = 0;

  VkImage m_volumeImage = VK_NULL_HANDLE;
  VkDeviceMemory m_volumeMemory = VK_NULL_HANDLE;
  VkImageView m_volumeView = VK_NULL_HANDLE;
  VkSampler m_volumeSampler = VK_NULL_HANDLE;

  VkImage m_transferImage = VK_NULL_HANDLE;
  VkDeviceMemory m_transferMemory = VK_NULL_HANDLE;
  VkImageView m_transferView = VK_NULL_HANDLE;
  VkSampler m_transferSampler = VK_NULL_HANDLE;
};

} // namespace gfxvulkan
