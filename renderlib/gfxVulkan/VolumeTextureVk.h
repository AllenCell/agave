#pragma once

#include "glm.h"
#include "resources/Buffer.h"
#include "resources/SampledImage.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

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

  bool valid() const { return m_volumeTexture.valid(); }
  VkImageView volumeView() const { return m_volumeTexture.view(); }
  VkSampler volumeSampler() const { return m_volumeTexture.sampler(); }
  VkImageView transferView() const { return m_transferTexture.view(); }
  VkSampler transferSampler() const { return m_transferTexture.sampler(); }

  glm::vec4 lutMin() const { return m_lutMin; }
  glm::vec4 lutMax() const { return m_lutMax; }
  glm::ivec3 dimensions() const { return m_dimensions; }
  size_t gpuBytes() const { return m_gpuBytes; }
  VolumeTextureMode mode() const { return m_mode; }
  bool linearFiltering() const { return m_linearFiltering; }

private:
  // Upload voxels produced directly into the staging buffer by `fill`.
  //
  // Inverting the control flow this way lets both upload modes write their
  // voxels exactly once: uploadRaw's channel interleave and uploadFused's fuse
  // both target the mapped staging memory, instead of filling a std::vector that
  // is then memcpy'd into staging. It also lets the image, its view, its sampler
  // and the staging buffer persist across uploads, which matters because a time
  // series re-uploads the same shape every frame.
  bool uploadVolumeFrom(const std::function<void(void* mapped)>& fill,
                        size_t byteCount,
                        VkFormat format,
                        uint32_t width,
                        uint32_t height,
                        uint32_t depth,
                        bool linearFiltering);

  // Ensure m_volumeTexture matches the requested shape, recreating it only when
  // it actually differs. Returns false on failure.
  bool ensureVolumeImage(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, bool linearFiltering);
  // Ensure the persistent staging buffer is at least byteCount and mapped.
  bool ensureStagingBuffer(size_t byteCount);
  bool uploadTransferBytes(const void* data, size_t byteCount);
  // Re-uploads bytes into the already-created m_transferImage. Assumes the
  // image is currently in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
  bool updateTransferBytes(const void* data, size_t byteCount);
  std::optional<resources::UniqueSampler> createSampler(bool linearFiltering, VkSamplerAddressMode addressMode);
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

  resources::SampledImage m_volumeTexture;
  resources::SampledImage m_transferTexture;

  // Shape the volume image was created with, so a re-upload of the same shape
  // can reuse it instead of destroying and recreating it every frame.
  VkFormat m_volumeFormat = VK_FORMAT_UNDEFINED;
  glm::ivec3 m_volumeExtent = glm::ivec3(0);

  // Staging buffer reused across uploads and left mapped, so each upload costs
  // neither an allocation nor a map/unmap pair.
  std::optional<resources::Buffer> m_staging;
  size_t m_stagingCapacity = 0;
  void* m_stagingMapped = nullptr;
};

} // namespace gfxvulkan
