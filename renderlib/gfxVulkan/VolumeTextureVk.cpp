#include "VolumeTextureVk.h"

#include "AppScene.h"
#include "Fuse.h"
#include "ImageXYZC.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"
#include "threading.h"
#include "shadersrc/fuse_comp_spv.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace gfxvulkan {

namespace {

constexpr float kInvUint16Max = 1.0f / 65535.0f;
// Each channel occupies ten scalar parameter words, a 256-entry float LUT,
// and a 256-entry packed RGB colormap. Keep this layout in sync with fuse.comp.
constexpr size_t kFuseScalarWords = 10;
constexpr size_t kFuseTableEntries = 256;
constexpr size_t kFuseColorMapOffset = kFuseScalarWords + kFuseTableEntries;
constexpr size_t kFuseWordsPerChannel = kFuseColorMapOffset + kFuseTableEntries;

uint32_t
floatBits(float value)
{
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
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
  if (!scene.m_volume) {
    release();
    return false;
  }

  m_mode = mode;
  // Deliberately NOT release() here. Releasing destroyed the image, view,
  // sampler and staging buffer on every upload, so stepping through a time
  // series recreated all of them per frame even though the shape never changes.
  // ensureVolumeImage recreates only when the format or extent actually differs.
  //
  // m_gpuBytes is recounted by the upload below; release() used to be what reset
  // it, so it has to be cleared explicitly now or it would accumulate.
  m_gpuBytes = 0;

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
  m_transferTexture.reset();
  m_volumeTexture.reset();
  m_fusePipeline.reset();
  m_fusePipelineLayout.reset();
  m_fusePool.reset();
  m_fuseSetLayout.reset();
  m_fuseSets.clear();
  if (m_fuseParams && m_fuseParamsMapped)
    vkUnmapMemory(m_backend.logicalDevice(), m_fuseParams->memory());
  m_fuseParamsMapped = nullptr;
  m_fuseParams.reset();
  m_fuseLimitsCached = false;
  m_fuseRaw.reset();
  m_fuseRawCapacity = 0;
  m_fuseChunkStride = 0;
  m_fuseChunkCount = 0;
  m_fuseImage.reset();
  m_majorantTexture.reset();
  m_majorantExtent = glm::ivec3(0);
  m_cellBounds = VolumeCellBounds();

  if (m_staging && m_stagingMapped) {
    vkUnmapMemory(m_backend.logicalDevice(), m_staging->memory());
  }
  m_stagingMapped = nullptr;
  m_stagingCapacity = 0;
  m_staging.reset();

  m_volumeFormat = VK_FORMAT_UNDEFINED;
  m_volumeExtent = glm::ivec3(0);
  m_volumeStorage = false;
  m_lastFuseWasGpu = false;
  m_fuseRawUploads = 0;

  m_dimensions = glm::ivec3(0);
  m_lutMin = glm::vec4(0.0f);
  m_lutMax = glm::vec4(1.0f);
  m_gpuBytes = 0;
  m_linearFiltering = false;
}

std::optional<resources::UniqueSampler>
VolumeTextureVk::createSampler(bool linearFiltering, VkSamplerAddressMode addressMode)
{
  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.minFilter = linearFiltering ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = addressMode;
  samplerInfo.addressModeV = addressMode;
  samplerInfo.addressModeW = addressMode;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  return m_backend.device().createSampler(samplerInfo);
}

bool
VolumeTextureVk::ensureStagingBuffer(size_t byteCount)
{
  if (m_staging && m_stagingCapacity >= byteCount && m_stagingMapped) {
    return true;
  }

  // Grown, never shrunk: a time series re-uploads the same size every frame, so
  // reallocating would just churn.
  if (m_staging && m_stagingMapped) {
    vkUnmapMemory(m_backend.logicalDevice(), m_staging->memory());
    m_stagingMapped = nullptr;
  }
  m_staging.reset();
  m_stagingCapacity = 0;

  auto staging =
    m_backend.device().createBuffer(static_cast<VkDeviceSize>(byteCount),
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return false;
  }

  void* mapped = nullptr;
  if (vkMapMemory(m_backend.logicalDevice(), staging->memory(), 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped) !=
      VK_SUCCESS) {
    return false;
  }

  m_staging = std::move(*staging);
  m_stagingCapacity = byteCount;
  // Left mapped for the lifetime of the buffer. HOST_COHERENT memory needs no
  // explicit flush, so there is nothing to do per upload.
  m_stagingMapped = mapped;
  return true;
}

bool
VolumeTextureVk::ensureVolumeImage(VkFormat format,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   bool linearFiltering)
{
  const glm::ivec3 extent(static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth));
  const bool wantStorage = m_mode == VolumeTextureMode::FusedRgba8 && m_fuseUseStorage;
  if (m_volumeTexture.valid() && m_volumeFormat == format && m_volumeExtent == extent &&
      (!wantStorage || m_volumeStorage)) {
    // Same shape as last time, which is the normal case when stepping through a
    // time series. Only the sampler might need to change.
    if (m_linearFiltering != linearFiltering) {
      if (!setLinearFiltering(linearFiltering)) {
        return false;
      }
    }
    return true;
  }

  auto image = m_backend.device().createImage(width,
                                              height,
                                              depth,
                                              1,
                                              format,
                                              VK_IMAGE_TYPE_3D,
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                (wantStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0));
  if (!image) {
    return false;
  }
  auto view =
    m_backend.device().createImageView(image->get(), format, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  if (!view) {
    return false;
  }
  // REPEAT matches the OpenGL 3D volume texture (ImageGpu::createVolumeTexture4x16
  // and Image3D::prepareTexture both use GL_REPEAT). CLAMP_TO_EDGE would
  // produce different color at the volume boundary when the ray sample point
  // falls just outside the volume due to floating-point error.
  auto sampler = createSampler(linearFiltering, VK_SAMPLER_ADDRESS_MODE_REPEAT);
  if (!sampler) {
    return false;
  }

  m_volumeTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));
  m_volumeFormat = format;
  m_volumeExtent = extent;
  m_volumeStorage = wantStorage;
  m_linearFiltering = linearFiltering;
  return true;
}

bool
VolumeTextureVk::uploadVolumeFrom(const std::function<void(void* mapped)>& fill,
                                  size_t byteCount,
                                  VkFormat format,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t depth,
                                  bool linearFiltering)
{
  if (!ensureStagingBuffer(byteCount)) {
    return false;
  }

  // The producer writes its voxels straight into mapped staging memory. No
  // intermediate vector, and no memcpy from one to the other.
  fill(m_stagingMapped);

  const bool imageExisted =
    m_volumeTexture.valid() && m_volumeFormat == format &&
    m_volumeExtent == glm::ivec3(static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth));
  if (!ensureVolumeImage(format, width, height, depth, linearFiltering)) {
    return false;
  }

  // One command buffer, one submit. Previously each of these three steps
  // submitted separately and waited for the queue to go idle, so a single
  // upload cost three full pipeline stalls.
  //
  // A reused image is in SHADER_READ_ONLY_OPTIMAL from the previous frame; a
  // freshly created one is UNDEFINED.
  const VkImageLayout oldLayout = imageExisted ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  transitionImageLayout(
    commandBuffer, m_volumeTexture.image(), VK_IMAGE_ASPECT_COLOR_BIT, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(commandBuffer, m_staging->get(), m_volumeTexture.image(), width, height, depth);
  transitionImageLayout(commandBuffer,
                        m_volumeTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_backend.endSingleTimeCommands(commandBuffer);

  m_dimensions = glm::ivec3(static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth));
  m_gpuBytes += byteCount;
  m_linearFiltering = linearFiltering;
  ++m_volumeGeneration;

  return true;
}

bool
VolumeTextureVk::uploadTransferBytes(const void* data, size_t byteCount)
{
  auto staging =
    m_backend.device().createBuffer(static_cast<VkDeviceSize>(byteCount),
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return false;
  }

  void* mapped = nullptr;
  if (vkMapMemory(m_backend.logicalDevice(), staging->memory(), 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped) !=
      VK_SUCCESS) {
    return false;
  }
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(m_backend.logicalDevice(), staging->memory());

  auto image = m_backend.device().createImage(kTransferSize,
                                              1,
                                              1,
                                              kTransferLayers,
                                              VK_FORMAT_R8G8B8A8_UNORM,
                                              VK_IMAGE_TYPE_2D,
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  if (!image) {
    return false;
  }
  auto view = m_backend.device().createImageView(
    image->get(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_ASPECT_COLOR_BIT, kTransferLayers);
  if (!view) {
    return false;
  }
  // CLAMP_TO_EDGE matches the OpenGL colormap 2D array
  // (ImageGpu::createVolumeTexture4x16 sets GL_CLAMP_TO_EDGE on
  // m_ActiveChannelColormaps).
  auto sampler = createSampler(false, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  if (!sampler) {
    return false;
  }

  transitionImageLayout(m_backend,
                        image->get(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        kTransferLayers);
  copyBufferToImage(m_backend, staging->get(), image->get(), kTransferSize, 1, 1, kTransferLayers);
  transitionImageLayout(m_backend,
                        image->get(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        kTransferLayers);

  m_transferTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));
  m_gpuBytes += byteCount;

  return true;
}

bool
VolumeTextureVk::updateTransferBytes(const void* data, size_t byteCount)
{
  if (!m_transferTexture) {
    return false;
  }

  auto staging =
    m_backend.device().createBuffer(static_cast<VkDeviceSize>(byteCount),
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return false;
  }

  void* mapped = nullptr;
  if (vkMapMemory(m_backend.logicalDevice(), staging->memory(), 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped) !=
      VK_SUCCESS) {
    return false;
  }
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(m_backend.logicalDevice(), staging->memory());

  transitionImageLayout(m_backend,
                        m_transferTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        kTransferLayers);
  copyBufferToImage(m_backend, staging->get(), m_transferTexture.image(), kTransferSize, 1, 1, kTransferLayers);
  transitionImageLayout(m_backend,
                        m_transferTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        kTransferLayers);

  return true;
}

bool
VolumeTextureVk::setLinearFiltering(bool linearFiltering)
{
  if (m_linearFiltering == linearFiltering && m_volumeTexture.sampler() != VK_NULL_HANDLE) {
    return false;
  }

  // Keep the volume-sampler wrap mode in sync with the initial upload path
  // (see uploadVolumeBytes): REPEAT to match the OpenGL 3D volume texture.
  auto newSampler = createSampler(linearFiltering, VK_SAMPLER_ADDRESS_MODE_REPEAT);
  if (!newSampler) {
    return false;
  }

  // Safe to replace immediately: endSingleTimeCommands() waits for the sampler
  // replacement submission to finish, and descriptor sets are re-written each
  // frame.
  m_volumeTexture.replaceSampler(std::move(*newSampler));
  m_linearFiltering = linearFiltering;
  return true;
}

bool
VolumeTextureVk::refreshColormap(const Scene& scene)
{
  // Only RawRgba16 keeps the per-channel colormap as a separate transfer
  // image; FusedRgba8 pre-multiplies material colors into the volume itself,
  // so the caller must trigger a full re-upload for that mode.
  if (m_mode != VolumeTextureMode::RawRgba16) {
    return false;
  }
  if (!m_transferTexture || !scene.m_volume) {
    return false;
  }

  const std::array<uint32_t, 4> channels = activeChannels(scene);
  std::array<uint8_t, kTransferSize * kTransferLayers * 4> transfer = {};
  buildRawTransferBytes(scene, channels, transfer);
  return updateTransferBytes(transfer.data(), transfer.size());
}

void
VolumeTextureVk::buildRawTransferBytes(const Scene& scene,
                                       const std::array<uint32_t, 4>& channels,
                                       std::array<uint8_t, kTransferSize * kTransferLayers * 4>& transfer)
{
  ImageXYZC* img = scene.m_volume.get();
  m_lutMin = glm::vec4(0.0f);
  m_lutMax = glm::vec4(1.0f);

  // The path-traced volume shader (pathTraceVolume.frag) samples g_colormapTexture
  // and applies g_diffuse[ch] / g_opacity[ch] / the transfer function on the fly.
  // Mirror the OpenGL backend (ImageGpu::updateLutGPU) and upload the RAW per-
  // channel colormap ramp here -- do NOT pre-bake diffuse, opacity, or the
  // channel LUT into it, or the shader will apply them a second time and the
  // rendered image will be too bright / saturated / low-contrast compared to
  // the OpenGL reference.
  for (uint32_t layer = 0; layer < kTransferLayers; ++layer) {
    const uint32_t channel = channels[layer];
    Channelu16* ch = img->channel(channel);
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

    const uint8_t* colormap =
      channel < MAX_CPU_CHANNELS ? scene.m_material.m_colormap[channel].m_colormap.data() : nullptr;

    for (uint32_t i = 0; i < kTransferSize; ++i) {
      const size_t srcIndex = i * 4;
      const size_t dstIndex = (layer * kTransferSize + i) * 4;
      if (colormap) {
        transfer[dstIndex + 0] = colormap[srcIndex + 0];
        transfer[dstIndex + 1] = colormap[srcIndex + 1];
        transfer[dstIndex + 2] = colormap[srcIndex + 2];
        transfer[dstIndex + 3] = colormap[srcIndex + 3];
      } else {
        transfer[dstIndex + 0] = 255;
        transfer[dstIndex + 1] = 255;
        transfer[dstIndex + 2] = 255;
        transfer[dstIndex + 3] = 255;
      }
    }
  }
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
VolumeTextureVk::ensureFusePipeline()
{
  if (m_fusePipeline) {
    return true;
  }

  std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
  for (uint32_t i = 0; i < bindings.size(); ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
  setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  setInfo.pBindings = bindings.data();
  auto setLayout = m_backend.device().createDescriptorSetLayout(setInfo);
  if (!setLayout)
    return false;
  m_fuseSetLayout = std::move(*setLayout);

  VkDescriptorSetLayout layout = m_fuseSetLayout.get();

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.size = sizeof(uint32_t) * 6;
  VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &layout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  auto pipelineLayout = m_backend.device().createPipelineLayout(layoutInfo);
  if (!pipelineLayout)
    return false;
  m_fusePipelineLayout = std::move(*pipelineLayout);

  auto shader = m_backend.device().createShaderModule(fuse_comp_spv, fuse_comp_spv_word_count);
  if (!shader)
    return false;
  VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = shader->get();
  stage.pName = "main";
  VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
  pipelineInfo.stage = stage;
  pipelineInfo.layout = m_fusePipelineLayout.get();
  auto pipeline = m_backend.device().createComputePipeline(pipelineInfo);
  if (!pipeline)
    return false;
  m_fusePipeline = std::move(*pipeline);
  return static_cast<bool>(m_fusePipeline);
}

bool
VolumeTextureVk::ensureFuseDescriptorSets(size_t count)
{
  if (m_fuseSets.size() >= count)
    return true;
  if (count == 0 || count > UINT32_MAX / 2)
    return false;
  m_fuseSets.clear();
  m_fusePool.reset();
  std::array<VkDescriptorPoolSize, 2> poolSizes{
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(count * 2) },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<uint32_t>(count) }
  };
  VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
  poolInfo.maxSets = static_cast<uint32_t>(count);
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  auto pool = m_backend.device().createDescriptorPool(poolInfo);
  if (!pool)
    return false;
  m_fusePool = std::move(*pool);
  std::vector<VkDescriptorSetLayout> layouts(count, m_fuseSetLayout.get());
  m_fuseSets.resize(count);
  VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
  alloc.descriptorPool = m_fusePool.get();
  alloc.descriptorSetCount = static_cast<uint32_t>(count);
  alloc.pSetLayouts = layouts.data();
  if (vkAllocateDescriptorSets(m_backend.logicalDevice(), &alloc, m_fuseSets.data()) != VK_SUCCESS) {
    m_fuseSets.clear();
    return false;
  }
  return true;
}

bool
VolumeTextureVk::uploadFused(const Scene& scene, bool linearFiltering)
{
  m_fuseUseStorage = true;
  ImageXYZC* img = scene.m_volume.get();
  const size_t voxelCount = img->sizeX() * img->sizeY() * img->sizeZ();
  const uint32_t channelCount = static_cast<uint32_t>(std::min(img->sizeC(), static_cast<size_t>(MAX_CPU_CHANNELS)));
  // No channel data can be fused. Check before using channelCount as the
  // descriptor-range divisor; the CPU fuse path also requires a channel.
  if (channelCount == 0)
    return false;
  // The physical device and its format support are fixed for this backend.
  // Query once, after headless/windowed device selection has completed.
  if (!m_fuseLimitsCached) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_backend.physicalDevice(), &properties);
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_backend.physicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
    m_fuseMaxGroups = static_cast<size_t>(std::min(properties.limits.maxComputeWorkGroupCount[0], 65535u)) *
                      properties.limits.maxComputeWorkGroupCount[1];
    m_fuseAlignment = std::max<size_t>(4, properties.limits.minStorageBufferOffsetAlignment);
    m_fuseDescriptorLimit = properties.limits.maxStorageBufferRange;
    m_fuseStorageSupported = (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    m_fuseLimitsCached = true;
  }
  const size_t maxChunkVoxels =
    std::min({ voxelCount,
               m_fuseChunkVoxelLimit,
               m_fuseDescriptorLimit > m_fuseAlignment
                 ? (m_fuseDescriptorLimit - m_fuseAlignment) / (channelCount * sizeof(uint16_t))
                 : 0,
               m_fuseMaxGroups * 256 });
  if (!m_fuseStorageSupported || maxChunkVoxels == 0 || voxelCount > UINT32_MAX || !ensureFusePipeline()) {
    return uploadFusedCpu(scene, linearFiltering);
  }
  const size_t chunkStride =
    ((maxChunkVoxels * channelCount * sizeof(uint16_t) + m_fuseAlignment - 1) / m_fuseAlignment) * m_fuseAlignment;
  const size_t chunkCount = (voxelCount + maxChunkVoxels - 1) / maxChunkVoxels;
  const size_t rawBufferBytes = chunkCount * chunkStride;
  // Current loaders create a new ImageXYZC for each time step, and no current
  // code edits its pixels in place after loading. If that changes, image
  // identity alone will no longer be enough to detect stale GPU raw data.
  const bool rawChanged = !m_fuseRaw || m_fuseRawCapacity < rawBufferBytes || m_fuseChunkStride != chunkStride ||
                          m_fuseChunkCount != chunkCount || m_fuseImage.lock() != scene.m_volume;
  if (!ensureFuseDescriptorSets(chunkCount) || (rawChanged && !ensureStagingBuffer(rawBufferBytes)))
    return uploadFusedCpu(scene, linearFiltering);
  if (!m_fuseRaw || m_fuseRawCapacity < rawBufferBytes) {
    auto buffer = m_backend.device().createBuffer(static_cast<VkDeviceSize>(rawBufferBytes),
                                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!buffer)
      return uploadFusedCpu(scene, linearFiltering);
    m_fuseRaw = std::move(*buffer);
    m_fuseRawCapacity = rawBufferBytes;
  }

  std::array<uint32_t, MAX_CPU_CHANNELS * kFuseWordsPerChannel> params{};
  for (uint32_t channel = 0; channel < channelCount; ++channel) {
    const size_t base = channel * kFuseWordsPerChannel;
    Channelu16* ch = img->channel(channel);
    const auto& material = scene.m_material;
    const float chmin = static_cast<float>(ch->m_histogram.getDataMin());
    const float chmax = static_cast<float>(ch->m_histogram.getDataMax());
    uint16_t gradientMin = 0;
    uint16_t gradientMax = 0;
    const bool hasMinMax = material.m_gradientData[channel].getMinMax(ch->m_histogram, &gradientMin, &gradientMax);
    params[base + 0] = floatBits(chmin);
    params[base + 1] = floatBits(chmax);
    params[base + 2] = floatBits(hasMinMax ? static_cast<float>(gradientMin) : chmin);
    params[base + 3] = floatBits(hasMinMax ? static_cast<float>(gradientMax) : chmax);
    params[base + 4] = floatBits(material.m_opacity[channel]);
    for (size_t component = 0; component < 3; ++component) {
      params[base + 5 + component] = floatBits(material.m_diffuse[channel * 3 + component]);
    }
    params[base + 8] = material.m_enabled[channel] ? 1u : 0u;
    params[base + 9] = material.m_labels[channel] > 0 ? 1u : 0u;
    const uint8_t* colormap = material.m_colormap[channel].m_colormap.data();
    for (size_t i = 0; i < kFuseTableEntries; ++i) {
      params[base + kFuseScalarWords + i] = floatBits(ch->m_lut[i]);
      params[base + kFuseColorMapOffset + i] = colormap ? static_cast<uint32_t>(colormap[i * 4]) |
                                                            (static_cast<uint32_t>(colormap[i * 4 + 1]) << 8u) |
                                                            (static_cast<uint32_t>(colormap[i * 4 + 2]) << 16u)
                                                        : 0x00ffffffu;
    }
  }
  if (!m_fuseParams) {
    auto buffer =
      m_backend.device().createBuffer(sizeof(params),
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer)
      return uploadFusedCpu(scene, linearFiltering);
    m_fuseParams = std::move(*buffer);
    if (vkMapMemory(m_backend.logicalDevice(), m_fuseParams->memory(), 0, sizeof(params), 0, &m_fuseParamsMapped) !=
        VK_SUCCESS) {
      m_fuseParams.reset();
      return uploadFusedCpu(scene, linearFiltering);
    }
  }
  // HOST_COHERENT and the synchronous fuse submission allow this mapping to
  // remain live across appearance updates, just like the raw staging buffer.
  std::memcpy(m_fuseParamsMapped, params.data(), sizeof(params));

  const uint32_t width = static_cast<uint32_t>(img->sizeX());
  const uint32_t height = static_cast<uint32_t>(img->sizeY());
  const uint32_t depth = static_cast<uint32_t>(img->sizeZ());
  const bool imageExisted = m_volumeTexture.valid() && m_volumeStorage && m_volumeFormat == VK_FORMAT_R8G8B8A8_UNORM &&
                            m_volumeExtent == glm::ivec3(width, height, depth);
  if (!ensureVolumeImage(VK_FORMAT_R8G8B8A8_UNORM, width, height, depth, linearFiltering))
    return uploadFusedCpu(scene, linearFiltering);

  // Pack channel-major chunks into one persistent GPU buffer. Each descriptor
  // exposes only one aligned window, satisfying maxStorageBufferRange without
  // splitting the upload or submitting once per chunk.
  if (rawChanged) {
    auto* staging = static_cast<uint8_t*>(m_stagingMapped);
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
      const size_t first = chunk * maxChunkVoxels;
      const size_t count = std::min(maxChunkVoxels, voxelCount - first);
      auto* destination = staging + chunk * chunkStride;
      for (uint32_t channel = 0; channel < channelCount; ++channel) {
        std::memcpy(destination + channel * count * sizeof(uint16_t),
                    img->ptr(channel) + first * sizeof(uint16_t),
                    count * sizeof(uint16_t));
      }
      std::memset(destination + count * channelCount * sizeof(uint16_t),
                  0,
                  chunkStride - count * channelCount * sizeof(uint16_t));
    }
  }

  VkDescriptorBufferInfo paramsInfo{ m_fuseParams->get(), 0, sizeof(params) };
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageView = m_volumeTexture.view();
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
    VkDescriptorBufferInfo rawInfo{ m_fuseRaw->get(),
                                    static_cast<VkDeviceSize>(chunk * chunkStride),
                                    static_cast<VkDeviceSize>(chunkStride) };
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = m_fuseSets[chunk];
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes[0].pBufferInfo = &rawInfo;
    writes[1].pBufferInfo = &paramsInfo;
    writes[2].pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  VkCommandBuffer command = m_backend.beginSingleTimeCommands();
  if (rawChanged) {
    VkBufferCopy region{ 0, 0, static_cast<VkDeviceSize>(rawBufferBytes) };
    vkCmdCopyBuffer(command, m_staging->get(), m_fuseRaw->get(), 1, &region);
    VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_fuseRaw->get();
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &barrier,
                         0,
                         nullptr);
  }
  transitionImageLayout(command,
                        m_volumeTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        imageExisted ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_fusePipeline.get());
  for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
    const size_t first = chunk * maxChunkVoxels;
    const size_t count = std::min(maxChunkVoxels, voxelCount - first);
    vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_COMPUTE, m_fusePipelineLayout.get(), 0, 1, &m_fuseSets[chunk], 0, nullptr);
    const std::array<uint32_t, 6> size{
      width, height, depth, channelCount, static_cast<uint32_t>(first), static_cast<uint32_t>(count)
    };
    vkCmdPushConstants(command, m_fusePipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(size), size.data());
    const size_t groups = (count + 255) / 256;
    vkCmdDispatch(command,
                  static_cast<uint32_t>(std::min(groups, size_t(65535))),
                  static_cast<uint32_t>((groups + 65534) / 65535),
                  1);
  }
  transitionImageLayout(command,
                        m_volumeTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_backend.endSingleTimeCommands(command);
  if (rawChanged)
    ++m_fuseRawUploads;

  m_dimensions = glm::ivec3(width, height, depth);
  m_fuseImage = scene.m_volume;
  m_fuseChunkStride = chunkStride;
  m_fuseChunkCount = chunkCount;
  m_lastFuseWasGpu = true;
  m_cellBounds = VolumeCellBounds();
  m_gpuBytes += rawBufferBytes + voxelCount * 4;
  ++m_volumeGeneration;

  // The raymarch descriptor layout still binds a transfer image, although
  // fused mode does not sample it.
  if (!m_transferTexture.valid()) {
    std::array<uint8_t, kTransferSize * kTransferLayers * 4> identity{};
    return uploadTransferBytes(identity.data(), identity.size());
  }
  return true;
}

bool
VolumeTextureVk::uploadFusedCpu(const Scene& scene, bool linearFiltering)
{
  m_fuseUseStorage = false;
  m_lastFuseWasGpu = false;
  m_fuseImage.reset();
  ImageXYZC* img = scene.m_volume.get();
  const size_t voxelCount = img->sizeX() * img->sizeY() * img->sizeZ();
  std::vector<glm::vec3> colors;
  colors.reserve(MAX_CPU_CHANNELS);
  for (int i = 0; i < MAX_CPU_CHANNELS; ++i) {
    colors.push_back(glm::vec3(
      scene.m_material.m_diffuse[i * 3], scene.m_material.m_diffuse[i * 3 + 1], scene.m_material.m_diffuse[i * 3 + 2]));
  }
  std::vector<uint8_t> rgba(voxelCount * 4);
  uint8_t* rgbaPtr = rgba.data();
  Fuse::fuse(img, colors, scene.m_material, &rgbaPtr, nullptr);
  auto fill = [&rgba](void* mapped) { std::memcpy(mapped, rgba.data(), rgba.size()); };
  m_cellBounds = VolumeCellBounds();
  if (!uploadVolumeFrom(fill,
                        rgba.size(),
                        VK_FORMAT_R8G8B8A8_UNORM,
                        static_cast<uint32_t>(img->sizeX()),
                        static_cast<uint32_t>(img->sizeY()),
                        static_cast<uint32_t>(img->sizeZ()),
                        linearFiltering))
    return false;
  if (!m_transferTexture.valid()) {
    std::array<uint8_t, kTransferSize * kTransferLayers * 4> identity{};
    return uploadTransferBytes(identity.data(), identity.size());
  }
  return true;
}

bool
VolumeTextureVk::uploadRaw(const Scene& scene, bool linearFiltering)
{
  ImageXYZC* img = scene.m_volume.get();
  const size_t voxelCount = img->sizeX() * img->sizeY() * img->sizeZ();
  const std::array<uint32_t, 4> channels = activeChannels(scene);

  std::array<uint8_t, kTransferSize * kTransferLayers * 4> transfer = {};
  buildRawTransferBytes(scene, channels, transfer);

  // Use the narrowest 16-bit format that holds every enabled channel. The
  // shaders only read components below the active channel count, and a
  // texture fetch fills missing components with 0 (and 1 for alpha), so a
  // one- or two-channel volume renders identically from R16 / RG16 while
  // costing a quarter or half of the memory traffic of RGBA16. Three channels
  // still use RGBA16 because RGB16 is not a required sampled format.
  uint32_t enabledCount = 0;
  // activeChannels() and the path-trace shader expose at most four slots.
  // Keep this count within that array before passing it to computeCellBounds().
  for (uint32_t c = 0; c < img->sizeC() && c < MAX_CPU_CHANNELS && enabledCount < channels.size(); ++c) {
    if (scene.m_material.m_enabled[c]) {
      ++enabledCount;
    }
  }
  uint32_t components = 4;
  VkFormat format = VK_FORMAT_R16G16B16A16_UNORM;
  if (enabledCount <= 1) {
    components = 1;
    format = VK_FORMAT_R16_UNORM;
  } else if (enabledCount == 2) {
    components = 2;
    format = VK_FORMAT_R16G16_UNORM;
  }

  // Interleave directly into staging: one pass over the volume, no intermediate
  // vector and no copy from it into the staging buffer.
  auto fill = [voxelCount, img, &channels, components](void* mapped) {
    uint16_t* out = static_cast<uint16_t*>(mapped);
    if (components == 1) {
      const uint16_t* src = img->channel(channels[0])->m_ptr;
      parallel_for(voxelCount,
                   [out, src](size_t s, size_t e) { std::memcpy(out + s, src + s, (e - s) * sizeof(uint16_t)); });
    } else if (components == 2) {
      const uint16_t* src0 = img->channel(channels[0])->m_ptr;
      const uint16_t* src1 = img->channel(channels[1])->m_ptr;
      parallel_for(voxelCount, [out, src0, src1](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
          out[i * 2 + 0] = src0[i];
          out[i * 2 + 1] = src1[i];
        }
      });
    } else {
      parallel_for(voxelCount, [out, img, &channels](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
          out[i * 4 + 0] = img->channel(channels[0])->m_ptr[i];
          out[i * 4 + 1] = img->channel(channels[1])->m_ptr[i];
          out[i * 4 + 2] = img->channel(channels[2])->m_ptr[i];
          out[i * 4 + 3] = img->channel(channels[3])->m_ptr[i];
        }
      });
    }
  };

  computeCellBounds(scene, channels, std::max<uint32_t>(1, enabledCount));

  return uploadVolumeFrom(fill,
                          voxelCount * components * sizeof(uint16_t),
                          format,
                          static_cast<uint32_t>(img->sizeX()),
                          static_cast<uint32_t>(img->sizeY()),
                          static_cast<uint32_t>(img->sizeZ()),
                          linearFiltering) &&
         uploadTransferBytes(transfer.data(), transfer.size());
}

void
VolumeTextureVk::computeCellBounds(const Scene& scene, const std::array<uint32_t, 4>& channels, uint32_t slots)
{
  ImageXYZC* img = scene.m_volume.get();
  const int nx = static_cast<int>(img->sizeX());
  const int ny = static_cast<int>(img->sizeY());
  const int nz = static_cast<int>(img->sizeZ());
  const int maxDim = std::max(nx, std::max(ny, nz));
  const int cell = std::max(1, (maxDim + kMajorantGridTarget - 1) / kMajorantGridTarget);

  VolumeCellBounds bounds;
  bounds.cellVoxels = cell;
  bounds.gridDim = glm::ivec3((nx + cell - 1) / cell, (ny + cell - 1) / cell, (nz + cell - 1) / cell);
  bounds.slots = slots;
  const size_t cellCount = bounds.cellCount();
  bounds.minValue.assign(cellCount * slots, 0xffff);
  bounds.maxValue.assign(cellCount * slots, 0);

  const int gx = bounds.gridDim.x;
  const int gy = bounds.gridDim.y;
  const size_t sliceStride = static_cast<size_t>(nx) * static_cast<size_t>(ny);

  // Distribute independent cells across threads. Wide, shallow volumes may
  // have only a few z-slabs, so parallelizing over z alone leaves cores idle.
  // Each cell scans its voxel block dilated by one voxel on every side;
  // indices wrap to match the REPEAT sampler at volume boundaries.
  const size_t gridSliceStride = static_cast<size_t>(gx) * static_cast<size_t>(gy);
  parallel_for(cellCount, [&](size_t s, size_t e) {
    for (size_t cellIndex = s; cellIndex < e; ++cellIndex) {
      const size_t cz = cellIndex / gridSliceStride;
      const size_t remainder = cellIndex % gridSliceStride;
      const int cy = static_cast<int>(remainder / static_cast<size_t>(gx));
      const int cx = static_cast<int>(remainder % static_cast<size_t>(gx));
      const int z0 = static_cast<int>(cz) * cell - 1;
      const int z1 = std::min(nz - 1, static_cast<int>(cz + 1) * cell); // inclusive, before wrap
      const int y0 = cy * cell - 1;
      const int y1 = std::min(ny - 1, (cy + 1) * cell);
      const int x0 = cx * cell - 1;
      const int x1 = std::min(nx - 1, (cx + 1) * cell);
      for (uint32_t slot = 0; slot < slots; ++slot) {
        const uint16_t* src = img->channel(channels[slot])->m_ptr;
        uint16_t lo = 0xffff;
        uint16_t hi = 0;
        for (int z = z0; z <= z1; ++z) {
          const int zz = (z + nz) % nz;
          for (int y = y0; y <= y1; ++y) {
            const int yy = (y + ny) % ny;
            const uint16_t* row = src + zz * sliceStride + static_cast<size_t>(yy) * nx;
            for (int x = x0; x <= x1; ++x) {
              const uint16_t v = row[(x + nx) % nx];
              lo = std::min(lo, v);
              hi = std::max(hi, v);
            }
          }
        }
        bounds.minValue[slot * cellCount + cellIndex] = lo;
        bounds.maxValue[slot * cellCount + cellIndex] = hi;
      }
    }
  });

  m_cellBounds = std::move(bounds);
}

bool
VolumeTextureVk::uploadMajorantGrid(const uint8_t* ratios, glm::ivec3 gridDim)
{
  if (!ratios || gridDim.x <= 0 || gridDim.y <= 0 || gridDim.z <= 0) {
    return false;
  }
  const size_t byteCount =
    static_cast<size_t>(gridDim.x) * static_cast<size_t>(gridDim.y) * static_cast<size_t>(gridDim.z);

  const bool imageExisted = m_majorantTexture.valid() && m_majorantExtent == gridDim;
  if (!imageExisted) {
    auto image = m_backend.device().createImage(static_cast<uint32_t>(gridDim.x),
                                                static_cast<uint32_t>(gridDim.y),
                                                static_cast<uint32_t>(gridDim.z),
                                                1,
                                                VK_FORMAT_R8_UNORM,
                                                VK_IMAGE_TYPE_3D,
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!image) {
      return false;
    }
    auto view = m_backend.device().createImageView(
      image->get(), VK_FORMAT_R8_UNORM, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (!view) {
      return false;
    }
    // Fetched with texelFetch, so the filter mode is irrelevant; the sampler is
    // only required by the combined-image-sampler binding.
    auto sampler = createSampler(false, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    if (!sampler) {
      return false;
    }
    m_majorantTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));
    m_majorantExtent = gridDim;
  }

  auto staging =
    m_backend.device().createBuffer(static_cast<VkDeviceSize>(byteCount),
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return false;
  }
  void* mapped = nullptr;
  if (vkMapMemory(m_backend.logicalDevice(), staging->memory(), 0, static_cast<VkDeviceSize>(byteCount), 0, &mapped) !=
      VK_SUCCESS) {
    return false;
  }
  std::memcpy(mapped, ratios, byteCount);
  vkUnmapMemory(m_backend.logicalDevice(), staging->memory());

  const VkImageLayout oldLayout = imageExisted ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  transitionImageLayout(commandBuffer,
                        m_majorantTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        oldLayout,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(commandBuffer,
                    staging->get(),
                    m_majorantTexture.image(),
                    static_cast<uint32_t>(gridDim.x),
                    static_cast<uint32_t>(gridDim.y),
                    static_cast<uint32_t>(gridDim.z));
  transitionImageLayout(commandBuffer,
                        m_majorantTexture.image(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_backend.endSingleTimeCommands(commandBuffer);
  return true;
}

} // namespace gfxvulkan
