#include "Device.h"

#include "Logging.h"

#include <shaderc/shaderc.hpp>

#include <cstring>
#include <utility>

namespace gfxvulkan {

namespace {

shaderc_shader_kind
toShadercKind(gfxApi::ShaderStage stage)
{
  switch (stage) {
    case gfxApi::ShaderStage::Vertex:
      return shaderc_vertex_shader;
    case gfxApi::ShaderStage::Fragment:
      return shaderc_fragment_shader;
    case gfxApi::ShaderStage::Geometry:
      return shaderc_geometry_shader;
    case gfxApi::ShaderStage::Compute:
      return shaderc_compute_shader;
  }
  return shaderc_vertex_shader;
}

std::vector<uint32_t>
compileGlslToSpirv(const gfxApi::ShaderDesc& desc)
{
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetSourceLanguage(shaderc_source_language_glsl);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
  options.SetOptimizationLevel(shaderc_optimization_level_performance);

  const char* name = desc.debugName.empty() ? "agave-vulkan-shader" : desc.debugName.c_str();
  shaderc::SpvCompilationResult result =
    compiler.CompileGlslToSpv(desc.source, toShadercKind(desc.stage), name, options);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    LOG_ERROR << "Vulkan GLSL compile failed (" << name << "): " << result.GetErrorMessage();
    return {};
  }

  return { result.cbegin(), result.cend() };
}

std::vector<uint32_t>
spirvFromString(const std::string& source, const std::string& debugName)
{
  if (source.size() % sizeof(uint32_t) != 0) {
    LOG_ERROR << "SPIR-V shader payload size is not 32-bit aligned (" << debugName << ")";
    return {};
  }

  std::vector<uint32_t> spirv(source.size() / sizeof(uint32_t));
  if (!spirv.empty()) {
    std::memcpy(spirv.data(), source.data(), source.size());
  }
  return spirv;
}

} // namespace

Device::Device() = default;

Device::~Device()
{
  release();
}

void
Device::initialize(VkPhysicalDevice physicalDevice, VkDevice device)
{
  m_physicalDevice = physicalDevice;
  m_device = device;
  m_resources = std::make_shared<resources::ResourceRegistry>(device);
}

void
Device::release()
{
  if (m_device == VK_NULL_HANDLE) {
    m_shaders.clear();
    m_programs.clear();
    m_resources.reset();
    m_physicalDevice = VK_NULL_HANDLE;
    return;
  }

  m_programs.clear();
  m_shaders.clear();

  if (m_resources) {
    const size_t remaining = m_resources->trackedResourceCount();
    if (remaining > 0) {
      LOG_WARNING << "gfxvulkan::Device releasing " << remaining
                  << " Vulkan resource(s) that outlived their normal owner";
    }
    m_resources->releaseAll();
    m_resources.reset();
  }

  m_device = VK_NULL_HANDLE;
  m_physicalDevice = VK_NULL_HANDLE;
}

gfxApi::ShaderHandle
Device::createShader(const gfxApi::ShaderDesc& desc)
{
  if (m_device == VK_NULL_HANDLE) {
    LOG_ERROR << "gfxvulkan::Device::createShader called before logical device initialization";
    return {};
  }

  std::vector<uint32_t> spirv;
  switch (desc.sourceKind) {
    case gfxApi::ShaderSourceKind::GLSL:
      spirv = compileGlslToSpirv(desc);
      break;
    case gfxApi::ShaderSourceKind::SPIRV:
      spirv = spirvFromString(desc.source, desc.debugName);
      break;
    case gfxApi::ShaderSourceKind::WGSL:
    default:
      LOG_ERROR << "gfxvulkan::Device does not accept WGSL shader sources";
      return {};
  }

  if (spirv.empty()) {
    return {};
  }

  auto shaderModule = createShaderModule(spirv.data(), spirv.size());
  if (!shaderModule) {
    LOG_ERROR << "Failed to create Vulkan shader module for " << desc.debugName;
    return {};
  }

  const uint64_t id = m_nextId++;
  m_shaders.emplace(id, ShaderRecord{ std::move(*shaderModule), desc.stage });
  return gfxApi::ShaderHandle{ id };
}

void
Device::destroyShader(gfxApi::ShaderHandle handle)
{
  auto it = m_shaders.find(handle.id);
  if (it == m_shaders.end()) {
    return;
  }

  m_shaders.erase(it);
}

gfxApi::ShaderProgramHandle
Device::createShaderProgram(const gfxApi::ShaderProgramDesc& desc)
{
  for (auto shaderHandle : desc.shaders) {
    if (m_shaders.find(shaderHandle.id) == m_shaders.end()) {
      LOG_ERROR << "gfxvulkan::Device::createShaderProgram: invalid shader handle";
      return {};
    }
  }

  const uint64_t id = m_nextId++;
  m_programs.emplace(id, ShaderProgramRecord{ desc.shaders });
  return gfxApi::ShaderProgramHandle{ id };
}

void
Device::destroyShaderProgram(gfxApi::ShaderProgramHandle handle)
{
  m_programs.erase(handle.id);
}

VkShaderModule
Device::shaderModule(gfxApi::ShaderHandle handle) const
{
  auto it = m_shaders.find(handle.id);
  return it == m_shaders.end() ? VK_NULL_HANDLE : it->second.module.get();
}

gfxApi::ShaderStage
Device::shaderStage(gfxApi::ShaderHandle handle) const
{
  auto it = m_shaders.find(handle.id);
  return it == m_shaders.end() ? gfxApi::ShaderStage::Vertex : it->second.stage;
}

std::optional<resources::Buffer>
Device::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
  if (m_device == VK_NULL_HANDLE || !m_resources || size == 0) {
    return std::nullopt;
  }

  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer buffer = VK_NULL_HANDLE;
  VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateBuffer failed with VkResult " << result;
    return std::nullopt;
  }

  VkMemoryRequirements memoryRequirements = {};
  vkGetBufferMemoryRequirements(m_device, buffer, &memoryRequirements);

  const uint32_t memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);
  if (memoryTypeIndex == UINT32_MAX) {
    vkDestroyBuffer(m_device, buffer, nullptr);
    return std::nullopt;
  }

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = memoryRequirements.size;
  allocateInfo.memoryTypeIndex = memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  result = vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkAllocateMemory for buffer failed with VkResult " << result;
    vkDestroyBuffer(m_device, buffer, nullptr);
    return std::nullopt;
  }

  result = vkBindBufferMemory(m_device, buffer, memory, 0);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkBindBufferMemory failed with VkResult " << result;
    vkDestroyBuffer(m_device, buffer, nullptr);
    vkFreeMemory(m_device, memory, nullptr);
    return std::nullopt;
  }

  resources::UniqueBuffer ownedBuffer(m_resources, buffer);
  resources::UniqueDeviceMemory ownedMemory(m_resources, memory);
  return resources::Buffer(std::move(ownedBuffer), std::move(ownedMemory), size);
}

std::optional<resources::Image>
Device::createImage(uint32_t width,
                    uint32_t height,
                    uint32_t depth,
                    uint32_t arrayLayers,
                    VkFormat format,
                    VkImageType imageType,
                    VkImageUsageFlags usage)
{
  if (m_device == VK_NULL_HANDLE || !m_resources || width == 0 || height == 0 || depth == 0 || arrayLayers == 0) {
    return std::nullopt;
  }

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = imageType;
  imageInfo.extent = { width, height, depth };
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = arrayLayers;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkImage image = VK_NULL_HANDLE;
  VkResult result = vkCreateImage(m_device, &imageInfo, nullptr, &image);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateImage failed with VkResult " << result;
    return std::nullopt;
  }

  VkMemoryRequirements memoryRequirements = {};
  vkGetImageMemoryRequirements(m_device, image, &memoryRequirements);

  const uint32_t memoryTypeIndex =
    findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryTypeIndex == UINT32_MAX) {
    vkDestroyImage(m_device, image, nullptr);
    return std::nullopt;
  }

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = memoryRequirements.size;
  allocateInfo.memoryTypeIndex = memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  result = vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkAllocateMemory for image failed with VkResult " << result;
    vkDestroyImage(m_device, image, nullptr);
    return std::nullopt;
  }

  result = vkBindImageMemory(m_device, image, memory, 0);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkBindImageMemory failed with VkResult " << result;
    vkDestroyImage(m_device, image, nullptr);
    vkFreeMemory(m_device, memory, nullptr);
    return std::nullopt;
  }

  resources::UniqueImage ownedImage(m_resources, image);
  resources::UniqueDeviceMemory ownedMemory(m_resources, memory);
  return resources::Image(std::move(ownedImage), std::move(ownedMemory));
}

std::optional<resources::UniqueImageView>
Device::createImageView(VkImage image,
                        VkFormat format,
                        VkImageViewType viewType,
                        VkImageAspectFlags aspect,
                        uint32_t layerCount)
{
  if (m_device == VK_NULL_HANDLE || !m_resources || image == VK_NULL_HANDLE || layerCount == 0) {
    return std::nullopt;
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = viewType;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = layerCount;

  VkImageView view = VK_NULL_HANDLE;
  VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &view);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateImageView failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueImageView(m_resources, view);
}

std::optional<resources::UniqueBufferView>
Device::createBufferView(const VkBufferViewCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources || createInfo.buffer == VK_NULL_HANDLE) {
    return std::nullopt;
  }

  VkBufferView view = VK_NULL_HANDLE;
  VkResult result = vkCreateBufferView(m_device, &createInfo, nullptr, &view);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateBufferView failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueBufferView(m_resources, view);
}

std::optional<resources::UniqueSampler>
Device::createSampler(const VkSamplerCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkSampler sampler = VK_NULL_HANDLE;
  VkResult result = vkCreateSampler(m_device, &createInfo, nullptr, &sampler);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateSampler failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueSampler(m_resources, sampler);
}

std::optional<resources::UniqueShaderModule>
Device::createShaderModule(const uint32_t* words, size_t wordCount)
{
  if (m_device == VK_NULL_HANDLE || !m_resources || !words || wordCount == 0) {
    return std::nullopt;
  }

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = wordCount * sizeof(uint32_t);
  createInfo.pCode = words;

  VkShaderModule module = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(m_device, &createInfo, nullptr, &module);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateShaderModule failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueShaderModule(m_resources, module);
}

std::optional<resources::UniqueDescriptorSetLayout>
Device::createDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  const VkResult result = vkCreateDescriptorSetLayout(m_device, &createInfo, nullptr, &layout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateDescriptorSetLayout failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueDescriptorSetLayout(m_resources, layout);
}

std::optional<resources::UniqueDescriptorPool>
Device::createDescriptorPool(const VkDescriptorPoolCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkDescriptorPool pool = VK_NULL_HANDLE;
  const VkResult result = vkCreateDescriptorPool(m_device, &createInfo, nullptr, &pool);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateDescriptorPool failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueDescriptorPool(m_resources, pool);
}

std::optional<resources::UniquePipelineLayout>
Device::createPipelineLayout(const VkPipelineLayoutCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkPipelineLayout layout = VK_NULL_HANDLE;
  const VkResult result = vkCreatePipelineLayout(m_device, &createInfo, nullptr, &layout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreatePipelineLayout failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniquePipelineLayout(m_resources, layout);
}

std::optional<resources::UniqueRenderPass>
Device::createRenderPass(const VkRenderPassCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkRenderPass renderPass = VK_NULL_HANDLE;
  const VkResult result = vkCreateRenderPass(m_device, &createInfo, nullptr, &renderPass);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateRenderPass failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueRenderPass(m_resources, renderPass);
}

std::optional<resources::UniquePipeline>
Device::createPipeline(const VkGraphicsPipelineCreateInfo& createInfo, VkPipelineCache cache)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult result = vkCreateGraphicsPipelines(m_device, cache, 1, &createInfo, nullptr, &pipeline);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateGraphicsPipelines failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniquePipeline(m_resources, pipeline);
}

std::optional<resources::UniqueFramebuffer>
Device::createFramebuffer(const VkFramebufferCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  const VkResult result = vkCreateFramebuffer(m_device, &createInfo, nullptr, &framebuffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFramebuffer failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueFramebuffer(m_resources, framebuffer);
}

std::optional<resources::UniqueFence>
Device::createFence(const VkFenceCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkFence fence = VK_NULL_HANDLE;
  const VkResult result = vkCreateFence(m_device, &createInfo, nullptr, &fence);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFence failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueFence(m_resources, fence);
}

std::optional<resources::UniqueCommandPool>
Device::createCommandPool(const VkCommandPoolCreateInfo& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkCommandPool commandPool = VK_NULL_HANDLE;
  const VkResult result = vkCreateCommandPool(m_device, &createInfo, nullptr, &commandPool);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateCommandPool failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueCommandPool(m_resources, commandPool);
}

std::optional<resources::UniqueSwapchain>
Device::createSwapchain(const VkSwapchainCreateInfoKHR& createInfo)
{
  if (m_device == VK_NULL_HANDLE || !m_resources) {
    return std::nullopt;
  }

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  const VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &swapchain);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateSwapchainKHR failed with VkResult " << result;
    return std::nullopt;
  }
  return resources::UniqueSwapchain(m_resources, swapchain);
}

uint32_t
Device::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
  if (m_physicalDevice == VK_NULL_HANDLE) {
    return UINT32_MAX;
  }

  VkPhysicalDeviceMemoryProperties memoryProperties = {};
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    const bool typeMatches = (typeFilter & (1u << i)) != 0;
    const bool propertiesMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
    if (typeMatches && propertiesMatch) {
      return i;
    }
  }

  LOG_ERROR << "Failed to find a compatible Vulkan memory type";
  return UINT32_MAX;
}

size_t
Device::trackedResourceCount() const
{
  return m_resources ? m_resources->trackedResourceCount() : 0;
}

} // namespace gfxvulkan
