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
  static constexpr uint32_t kTransferSize = 256;
  static constexpr uint32_t kTransferLayers = 4;

  explicit VolumeTextureVk(Backend& backend);
  ~VolumeTextureVk();

  bool upload(const Scene& scene, VolumeTextureMode mode, bool linearFiltering);
  void release();

  // Rebuild only the per-channel colormap ("transfer") image from the current
  // scene material state without touching the volume voxel data. Cheap; use in
  // response to TransferFunctionDirty. Returns false if the current mode does
  // not support a colormap-only refresh (e.g. FusedRgba8 bakes colors into the
  // volume itself, so the caller must trigger a full upload instead).
  bool refreshColormap(const Scene& scene);

  // Recreate the volume sampler with a different filtering mode. Cheap; use in
  // response to RenderParamsDirty when only the interpolation setting changed.
  // Returns true if the sampler was actually recreated. The descriptor set
  // referring to the sampler is re-written every frame by the renderer, so no
  // extra work is required by the caller.
  bool setLinearFiltering(bool linearFiltering);

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
  bool linearFiltering() const { return m_linearFiltering; }

private:
  bool uploadVolumeBytes(const void* data,
                         size_t byteCount,
                         VkFormat format,
                         uint32_t width,
                         uint32_t height,
                         uint32_t depth,
                         bool linearFiltering);
  bool uploadTransferBytes(const void* data, size_t byteCount);
  // Re-uploads bytes into the already-created m_transferImage. Assumes the
  // image is currently in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
  bool updateTransferBytes(const void* data, size_t byteCount);
  bool createSampler(bool linearFiltering, VkSampler& sampler);
  bool uploadFused(const Scene& scene, bool linearFiltering);
  bool uploadRaw(const Scene& scene, bool linearFiltering);
  std::array<uint32_t, 4> activeChannels(const Scene& scene) const;

  // Fill `transfer` with the per-channel colormap bytes used by RawRgba16 mode
  // and refresh m_lutMin / m_lutMax for the active channels. Shared by
  // uploadRaw and refreshColormap.
  void buildRawTransferBytes(const Scene& scene,
                             const std::array<uint32_t, 4>& channels,
                             std::array<uint8_t, kTransferSize * kTransferLayers * 4>& transfer);

  Backend& m_backend;
  VolumeTextureMode m_mode = VolumeTextureMode::FusedRgba8;
  glm::ivec3 m_dimensions = glm::ivec3(0);
  glm::vec4 m_lutMin = glm::vec4(0.0f);
  glm::vec4 m_lutMax = glm::vec4(1.0f);
  size_t m_gpuBytes = 0;
  bool m_linearFiltering = false;

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
