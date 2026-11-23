#include "renderlib/AppScene.h"
#include "renderlib/Fuse.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/gfxVulkan/Backend.h"
#include "renderlib/gfxVulkan/Device.h"
#include "renderlib/gfxVulkan/VolumeTextureVk.h"
#include "shadersrc/readVolume_comp_spv.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

std::vector<uint8_t>
readVolume(gfxvulkan::Backend& backend, gfxvulkan::VolumeTextureVk& volume, uint32_t x, uint32_t y, uint32_t z)
{
  const size_t bytes = static_cast<size_t>(x) * y * z * 4;
  auto buffer =
    backend.device().createBuffer(bytes,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  REQUIRE(buffer);
  std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
  setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  setInfo.pBindings = bindings.data();
  auto setLayout = backend.device().createDescriptorSetLayout(setInfo);
  REQUIRE(setLayout);
  VkDescriptorSetLayout layout = setLayout->get();
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.size = 3 * sizeof(uint32_t);
  VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &layout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  auto pipelineLayout = backend.device().createPipelineLayout(layoutInfo);
  REQUIRE(pipelineLayout);
  auto shader = backend.device().createShaderModule(readVolume_comp_spv, readVolume_comp_spv_word_count);
  REQUIRE(shader);
  VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
  pipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = shader->get();
  pipelineInfo.stage.pName = "main";
  pipelineInfo.layout = pipelineLayout->get();
  auto pipeline = backend.device().createComputePipeline(pipelineInfo);
  REQUIRE(pipeline);
  std::array<VkDescriptorPoolSize, 2> poolSizes = {
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
  };
  VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  auto pool = backend.device().createDescriptorPool(poolInfo);
  REQUIRE(pool);
  VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
  allocateInfo.descriptorPool = pool->get();
  allocateInfo.descriptorSetCount = 1;
  allocateInfo.pSetLayouts = &layout;
  VkDescriptorSet set = VK_NULL_HANDLE;
  REQUIRE(vkAllocateDescriptorSets(backend.logicalDevice(), &allocateInfo, &set) == VK_SUCCESS);
  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = volume.volumeSampler();
  imageInfo.imageView = volume.volumeView();
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorBufferInfo bufferInfo{ buffer->get(), 0, bytes };
  std::array<VkWriteDescriptorSet, 2> writes{};
  writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
  writes[0].dstSet = set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &imageInfo;
  writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
  writes[1].dstSet = set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].pBufferInfo = &bufferInfo;
  vkUpdateDescriptorSets(backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  VkCommandBuffer command = backend.beginSingleTimeCommands();
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->get());
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout->get(), 0, 1, &set, 0, nullptr);
  const std::array<uint32_t, 3> dimensions{ x, y, z };
  vkCmdPushConstants(command, pipelineLayout->get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dimensions), dimensions.data());
  vkCmdDispatch(command, static_cast<uint32_t>((bytes / 4 + 63) / 64), 1, 1);
  backend.endSingleTimeCommands(command);

  void* mapped = nullptr;
  REQUIRE(vkMapMemory(backend.logicalDevice(), buffer->memory(), 0, bytes, 0, &mapped) == VK_SUCCESS);
  std::vector<uint8_t> pixels(bytes);
  std::memcpy(pixels.data(), mapped, bytes);
  vkUnmapMemory(backend.logicalDevice(), buffer->memory());
  return pixels;
}

TEST_CASE("Benchmark Vulkan fuse appearance updates", "[.][vulkan][fuse][benchmark]")
{
  if (!std::getenv("AGAVE_BENCH_FUSE"))
    SKIP("Set AGAVE_BENCH_FUSE to run the fuse benchmark");

  gfxApi::InitParams params;
  params.backendKind = gfxApi::BackendKind::Vulkan;
  params.headless = true;
  gfxvulkan::Backend backend(params);
  if (!backend.isValid() || !backend.initDeviceHeadless())
    SKIP("No headless Vulkan device available");

  constexpr uint32_t side = 128;
  constexpr size_t voxels = static_cast<size_t>(side) * side * side;
  for (uint32_t channels : { 2u, 8u }) {
    auto* bytes = new uint8_t[voxels * channels * sizeof(uint16_t)];
    auto* raw = reinterpret_cast<uint16_t*>(bytes);
    for (size_t i = 0; i < voxels * channels; ++i)
      raw[i] = static_cast<uint16_t>((i * 7919u) & 65535u);
    auto image = std::make_shared<ImageXYZC>(side, side, side, channels, 16, bytes);
    Scene scene;
    scene.m_volume = image;
    scene.initSceneFromImg(image);
    for (uint32_t channel = 0; channel < channels; ++channel)
      scene.m_material.m_enabled[channel] = true;
    gfxvulkan::VolumeTextureVk volume(backend);
    REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
    REQUIRE(volume.lastFuseUsedGpu());
    std::vector<double> milliseconds;
    for (int i = 0; i < 18; ++i) {
      scene.m_material.m_opacity[0] = 0.4f + 0.01f * static_cast<float>(i);
      const auto start = std::chrono::steady_clock::now();
      REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
      const auto end = std::chrono::steady_clock::now();
      if (i >= 3)
        milliseconds.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    REQUIRE(volume.fuseRawUploadCount() == 1);
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "FUSE_BENCH channels=" << channels << " voxels=" << voxels
              << " median_ms=" << milliseconds[milliseconds.size() / 2]
              << " min_ms=" << milliseconds.front() << " max_ms=" << milliseconds.back() << std::endl;
  }
}

} // namespace

TEST_CASE("Vulkan compute fuse matches CPU labels and opacity", "[vulkan][fuse]")
{
  gfxApi::InitParams params;
  params.backendKind = gfxApi::BackendKind::Vulkan;
  params.headless = true;
  gfxvulkan::Backend backend(params);
  if (!backend.isValid() || !backend.initDeviceHeadless()) {
    SKIP("No headless Vulkan device available");
  }

  SECTION("Empty channel set is rejected before chunk sizing") {
    Scene emptyScene;
    emptyScene.m_volume = std::make_shared<ImageXYZC>(1, 1, 1, 0, 16, nullptr);
    gfxvulkan::VolumeTextureVk emptyVolume(backend);
    REQUIRE_FALSE(emptyVolume.upload(emptyScene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
  }

  constexpr uint32_t x = 3, y = 2, z = 2, channels = 2;
  constexpr size_t voxels = x * y * z;
  auto* bytes = new uint8_t[voxels * channels * sizeof(uint16_t)];
  auto* raw = reinterpret_cast<uint16_t*>(bytes);
  for (size_t i = 0; i < voxels; ++i) {
    raw[i] = static_cast<uint16_t>((i * 23) % 256);
    raw[voxels + i] = static_cast<uint16_t>((i * 41) % 256);
  }
  auto image = std::make_shared<ImageXYZC>(x, y, z, channels, 16, bytes);
  Scene scene;
  scene.m_volume = image;
  scene.initSceneFromImg(image);
  scene.m_material.m_enabled[0] = true;
  scene.m_material.m_enabled[1] = true;
  scene.m_material.m_labels[0] = 1.0f;
  scene.m_material.m_opacity[0] = 0.4f;
  scene.m_material.m_opacity[1] = 0.7f;
  scene.m_material.m_diffuse[0] = 0.0f;
  scene.m_material.m_diffuse[1] = 0.0f;
  scene.m_material.m_diffuse[2] = 0.0f;
  scene.m_material.m_diffuse[3] = 0.0f;
  scene.m_material.m_diffuse[4] = 0.0f;
  scene.m_material.m_diffuse[5] = 0.0f;
  scene.m_material.m_colormap[0].m_colormap.resize(256 * 4);
  for (size_t i = 0; i < 256; ++i) {
    scene.m_material.m_colormap[0].m_colormap[i * 4 + 0] = static_cast<uint8_t>(i);
    scene.m_material.m_colormap[0].m_colormap[i * 4 + 1] = 128;
    scene.m_material.m_colormap[0].m_colormap[i * 4 + 2] = 255;
    scene.m_material.m_colormap[0].m_colormap[i * 4 + 3] = 255;
  }

  gfxvulkan::VolumeTextureVk volume(backend);
  REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
  REQUIRE(volume.lastFuseUsedGpu());
  REQUIRE(volume.fuseRawUploadCount() == 1);
  std::vector<uint8_t> gpu = readVolume(backend, volume, x, y, z);
  std::vector<uint8_t> cpu(gpu.size());
  uint8_t* cpuPtr = cpu.data();
  std::vector<glm::vec3> colors;
  for (uint32_t channel = 0; channel < channels; ++channel) {
    colors.emplace_back(scene.m_material.m_diffuse[channel * 3],
                        scene.m_material.m_diffuse[channel * 3 + 1],
                        scene.m_material.m_diffuse[channel * 3 + 2]);
  }
  Fuse::fuse(image.get(), colors, scene.m_material, &cpuPtr, nullptr);
  for (size_t i = 0; i < cpu.size(); ++i) {
    INFO("byte " << i << ": gpu=" << static_cast<int>(gpu[i]) << " cpu=" << static_cast<int>(cpu[i]));
    REQUIRE(std::abs(static_cast<int>(gpu[i]) - static_cast<int>(cpu[i])) <= 1);
  }

  scene.m_material.m_diffuse[0] = 1.0f;
  REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
  REQUIRE(volume.lastFuseUsedGpu());
  REQUIRE(volume.fuseRawUploadCount() == 1);
  REQUIRE(readVolume(backend, volume, x, y, z) != gpu);
  scene.m_material.m_diffuse[0] = 0.0f;

  volume.setFuseChunkVoxelLimitForTesting(5);
  REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
  REQUIRE(volume.lastFuseUsedGpu());
  REQUIRE(volume.fuseRawUploadCount() == 2);
  const auto chunked = readVolume(backend, volume, x, y, z);
  for (size_t i = 0; i < cpu.size(); ++i) {
    REQUIRE(std::abs(static_cast<int>(chunked[i]) - static_cast<int>(cpu[i])) <= 1);
  }
  volume.release();
  REQUIRE(volume.upload(scene, gfxvulkan::VolumeTextureMode::FusedRgba8, false));
  REQUIRE(volume.lastFuseUsedGpu());
}
