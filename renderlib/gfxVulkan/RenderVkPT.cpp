#include "RenderVkPT.h"

#include "CCamera.h"
#include "Framebuffer.h"
#include "Logging.h"
#include "RenderSettings.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/shadersrc/ptAccum_frag_spv.hpp"
#include "gfxVulkan/shadersrc/ptAccum_vert_spv.hpp"
#include "gfxVulkan/shadersrc/toneMap_frag_spv.hpp"
#include "gfxVulkan/shadersrc/toneMap_vert_spv.hpp"
#include "glm.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

namespace gfxvulkan {

namespace {

struct FullscreenVertex
{
  float position[3];
  float uv[2];
};

struct alignas(16) PtAccumUniforms
{
  glm::mat4 inverseModelViewMatrix = glm::mat4(1.0f);
  int numIterations = 1;
  int padding[15] = {};
};

struct alignas(16) ToneMapUniforms
{
  float inverseExposure = 1.0f;
  float padding[3] = {};
};

const std::array<FullscreenVertex, 4> kFullscreenVertices = {
  FullscreenVertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
  FullscreenVertex{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
  FullscreenVertex{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
  FullscreenVertex{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
};

const std::array<uint16_t, 6> kFullscreenIndices = { 0, 1, 2, 2, 3, 0 };

template<typename T>
bool
uploadHostBuffer(Backend& backend, VkBufferUsageFlags usage, const T* data, size_t count, VkBuffer& buffer, VkDeviceMemory& memory)
{
  const VkDeviceSize byteCount = static_cast<VkDeviceSize>(sizeof(T) * count);
  if (!createBuffer(backend,
                    byteCount,
                    usage,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    buffer,
                    memory)) {
    return false;
  }

  void* mapped = nullptr;
  vkMapMemory(backend.logicalDevice(), memory, 0, byteCount, 0, &mapped);
  std::memcpy(mapped, data, static_cast<size_t>(byteCount));
  vkUnmapMemory(backend.logicalDevice(), memory);
  return true;
}

bool
createUniformBuffer(Backend& backend, VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory)
{
  return createBuffer(backend,
                      size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buffer,
                      memory);
}

bool
createColorRenderPass(Backend& backend, VkFormat colorFormat, VkRenderPass& renderPass)
{
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = colorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  VkResult result = vkCreateRenderPass(backend.logicalDevice(), &renderPassInfo, nullptr, &renderPass);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateRenderPass failed with VkResult " << result;
    return false;
  }
  return true;
}

} // namespace

const std::string RenderVkPT::TYPE_NAME = "pathtrace";

RenderVkPT::RenderVkPT(Backend& backend, RenderSettings* renderSettings)
  : RenderVk(backend, renderSettings)
{
}

RenderVkPT::~RenderVkPT()
{
  destroyFullscreenResources();
}

VolumeTextureMode
RenderVkPT::volumeTextureMode() const
{
  return VolumeTextureMode::RawRgba16;
}

bool
RenderVkPT::usesProgressiveAccumulation() const
{
  return true;
}

float
RenderVkPT::volumeShaderMode() const
{
  return 1.0f;
}

float
RenderVkPT::rayStepCount() const
{
  return 768.0f;
}

void
RenderVkPT::render(const CCamera& camera)
{
  if (m_w == 0 || m_h == 0) {
    return;
  }

  if (!m_displayFramebuffer || m_displayFramebuffer->width() != m_w || m_displayFramebuffer->height() != m_h) {
    gfxApi::FramebufferDesc desc;
    desc.width = m_w;
    desc.height = m_h;
    desc.colorFormat = gfxApi::FramebufferColorFormat::Rgba8;
    desc.depthStencil = false;
    m_displayFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);
  }

  renderToFramebufferPT(camera, *m_displayFramebuffer);
}

void
RenderVkPT::renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo)
{
  auto* vkFramebuffer = dynamic_cast<Framebuffer*>(fbo);
  if (!vkFramebuffer) {
    LOG_ERROR << "gfxvulkan::RenderVkPT::renderTo requires a Vulkan framebuffer";
    return;
  }
  renderToFramebufferPT(camera, *vkFramebuffer);
}

void
RenderVkPT::resize(uint32_t w, uint32_t h)
{
  RenderVk::resize(w, h);
  m_displayFramebuffer.reset();
  m_sampleFramebuffer.reset();
  m_accumFramebuffer.reset();
  m_accumScratchFramebuffer.reset();
}

void
RenderVkPT::cleanUpResources()
{
  destroyFullscreenResources();
  RenderVk::cleanUpResources();
}

bool
RenderVkPT::ensureFramebuffers(uint32_t w, uint32_t h)
{
  if (w == 0 || h == 0) {
    return false;
  }

  if (m_sampleFramebuffer && m_sampleFramebuffer->width() == w && m_sampleFramebuffer->height() == h &&
      m_accumFramebuffer && m_accumFramebuffer->width() == w && m_accumFramebuffer->height() == h &&
      m_accumScratchFramebuffer && m_accumScratchFramebuffer->width() == w &&
      m_accumScratchFramebuffer->height() == h) {
    return true;
  }

  gfxApi::FramebufferDesc desc;
  desc.width = w;
  desc.height = h;
  desc.colorFormat = gfxApi::FramebufferColorFormat::Rgba32F;
  desc.depthStencil = false;
  m_sampleFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);
  m_accumFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);
  m_accumScratchFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);

  const gfxApi::ClearColor clear = {};
  m_sampleFramebuffer->clear(clear);
  m_accumFramebuffer->clear(clear);
  m_accumScratchFramebuffer->clear(clear);
  return true;
}

bool
RenderVkPT::ensureFullscreenResources(VkFormat toneMapFormat)
{
  VkDevice device = m_backend.logicalDevice();

  if (m_quadVertexBuffer == VK_NULL_HANDLE &&
      !uploadHostBuffer(m_backend,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        kFullscreenVertices.data(),
                        kFullscreenVertices.size(),
                        m_quadVertexBuffer,
                        m_quadVertexMemory)) {
    return false;
  }

  if (m_quadIndexBuffer == VK_NULL_HANDLE &&
      !uploadHostBuffer(m_backend,
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        kFullscreenIndices.data(),
                        kFullscreenIndices.size(),
                        m_quadIndexBuffer,
                        m_quadIndexMemory)) {
    return false;
  }
  m_quadIndexCount = static_cast<uint32_t>(kFullscreenIndices.size());

  if (m_accumUniformBuffer == VK_NULL_HANDLE &&
      !createUniformBuffer(m_backend, sizeof(PtAccumUniforms), m_accumUniformBuffer, m_accumUniformMemory)) {
    return false;
  }
  if (m_toneMapUniformBuffer == VK_NULL_HANDLE &&
      !createUniformBuffer(m_backend, sizeof(ToneMapUniforms), m_toneMapUniformBuffer, m_toneMapUniformMemory)) {
    return false;
  }

  if (m_framebufferSampler == VK_NULL_HANDLE) {
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    VkResult result = vkCreateSampler(device, &samplerInfo, nullptr, &m_framebufferSampler);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateSampler for pathtrace framebuffer sampling failed with VkResult " << result;
      return false;
    }
  }

  if (m_accumDescriptorSetLayout == VK_NULL_HANDLE) {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_accumDescriptorSetLayout);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorSetLayout for pathtrace accumulation failed with VkResult " << result;
      return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_accumDescriptorPool);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorPool for pathtrace accumulation failed with VkResult " << result;
      return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_accumDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_accumDescriptorSetLayout;
    result = vkAllocateDescriptorSets(device, &allocateInfo, &m_accumDescriptorSet);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for pathtrace accumulation failed with VkResult " << result;
      return false;
    }
  }

  if (m_toneMapDescriptorSetLayout == VK_NULL_HANDLE) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_toneMapDescriptorSetLayout);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorSetLayout for pathtrace tone map failed with VkResult " << result;
      return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_toneMapDescriptorPool);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorPool for pathtrace tone map failed with VkResult " << result;
      return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_toneMapDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_toneMapDescriptorSetLayout;
    result = vkAllocateDescriptorSets(device, &allocateInfo, &m_toneMapDescriptorSet);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for pathtrace tone map failed with VkResult " << result;
      return false;
    }
  }

  if (m_accumPipeline != VK_NULL_HANDLE && m_toneMapPipeline != VK_NULL_HANDLE &&
      m_toneMapPipelineColorFormat == toneMapFormat) {
    return true;
  }

  destroyPipelines();
  m_toneMapPipelineColorFormat = toneMapFormat;

  if (!createColorRenderPass(m_backend, VK_FORMAT_R32G32B32A32_SFLOAT, m_accumRenderPass) ||
      !createColorRenderPass(m_backend, toneMapFormat, m_toneMapRenderPass)) {
    return false;
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_accumDescriptorSetLayout;
  VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_accumPipelineLayout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreatePipelineLayout for pathtrace accumulation failed with VkResult " << result;
    return false;
  }

  pipelineLayoutInfo.pSetLayouts = &m_toneMapDescriptorSetLayout;
  result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_toneMapPipelineLayout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreatePipelineLayout for pathtrace tone map failed with VkResult " << result;
    return false;
  }

  auto createFullscreenPipeline =
    [&](VkRenderPass renderPass,
        VkPipelineLayout pipelineLayout,
        const uint32_t* vertexWords,
        size_t vertexWordCount,
        const uint32_t* fragmentWords,
        size_t fragmentWordCount,
        bool includeUv,
        VkPipeline& pipeline) -> bool {
    VkShaderModule vertexShader = createShaderModule(vertexWords, vertexWordCount);
    VkShaderModule fragmentShader = createShaderModule(fragmentWords, fragmentWordCount);
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
      if (vertexShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertexShader, nullptr);
      }
      if (fragmentShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragmentShader, nullptr);
      }
      return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage = {};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragmentStage = {};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = { vertexStage, fragmentStage };

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(FullscreenVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes = {};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = includeUv ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(FullscreenVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(FullscreenVertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = includeUv ? 2 : 1;
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkCreateGraphicsPipelines for pathtrace fullscreen pass failed with VkResult " << result;
      return false;
    }
    return true;
  };

  return createFullscreenPipeline(m_accumRenderPass,
                                  m_accumPipelineLayout,
                                  ptAccum_vert_spv,
                                  ptAccum_vert_spv_word_count,
                                  ptAccum_frag_spv,
                                  ptAccum_frag_spv_word_count,
                                  false,
                                  m_accumPipeline) &&
         createFullscreenPipeline(m_toneMapRenderPass,
                                  m_toneMapPipelineLayout,
                                  toneMap_vert_spv,
                                  toneMap_vert_spv_word_count,
                                  toneMap_frag_spv,
                                  toneMap_frag_spv_word_count,
                                  true,
                                  m_toneMapPipeline);
}

bool
RenderVkPT::updateAccumUniformBuffer()
{
  PtAccumUniforms uniforms;
  uniforms.numIterations = std::max(1, m_renderSettings ? m_renderSettings->GetNoIterations() : 1);

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), m_accumUniformMemory, 0, sizeof(PtAccumUniforms), 0, &mapped);
  std::memcpy(mapped, &uniforms, sizeof(PtAccumUniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_accumUniformMemory);
  return true;
}

bool
RenderVkPT::updateToneMapUniformBuffer(const CCamera& camera)
{
  ToneMapUniforms uniforms;
  uniforms.inverseExposure = 1.0f / std::max(camera.m_Film.m_Exposure, 0.0001f);

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), m_toneMapUniformMemory, 0, sizeof(ToneMapUniforms), 0, &mapped);
  std::memcpy(mapped, &uniforms, sizeof(ToneMapUniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_toneMapUniformMemory);
  return true;
}

bool
RenderVkPT::updateAccumDescriptorSet()
{
  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = m_accumUniformBuffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(PtAccumUniforms);

  VkDescriptorImageInfo renderInfo = {};
  renderInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  renderInfo.imageView = m_sampleFramebuffer->colorImageView();
  renderInfo.sampler = m_framebufferSampler;

  VkDescriptorImageInfo accumInfo = {};
  accumInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  accumInfo.imageView = m_accumFramebuffer->colorImageView();
  accumInfo.sampler = m_framebufferSampler;

  std::array<VkWriteDescriptorSet, 3> writes = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = m_accumDescriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufferInfo;
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = m_accumDescriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].descriptorCount = 1;
  writes[1].pImageInfo = &renderInfo;
  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = m_accumDescriptorSet;
  writes[2].dstBinding = 2;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[2].descriptorCount = 1;
  writes[2].pImageInfo = &accumInfo;

  vkUpdateDescriptorSets(m_backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return true;
}

bool
RenderVkPT::updateToneMapDescriptorSet()
{
  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = m_toneMapUniformBuffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(ToneMapUniforms);

  VkDescriptorImageInfo accumInfo = {};
  accumInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  accumInfo.imageView = m_accumFramebuffer->colorImageView();
  accumInfo.sampler = m_framebufferSampler;

  std::array<VkWriteDescriptorSet, 2> writes = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = m_toneMapDescriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufferInfo;
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = m_toneMapDescriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].descriptorCount = 1;
  writes[1].pImageInfo = &accumInfo;

  vkUpdateDescriptorSets(m_backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return true;
}

void
RenderVkPT::renderToFramebufferPT(const CCamera& camera, Framebuffer& framebuffer)
{
  if (!m_scene || !m_scene->m_volume || !m_renderSettings) {
    framebuffer.clear({});
    return;
  }

  const uint32_t renderWidth = std::max<uint32_t>(1, m_w);
  const uint32_t renderHeight = std::max<uint32_t>(1, m_h);
  if (!ensureFramebuffers(renderWidth, renderHeight) || !ensureFullscreenResources(framebuffer.colorFormat())) {
    framebuffer.clear({});
    return;
  }

  const int exposureIterations = std::max(1, camera.m_Film.m_ExposureIterations);
  for (int i = 0; i < exposureIterations; ++i) {
    renderToFramebuffer(camera, *m_sampleFramebuffer);

    transitionToShaderRead(*m_sampleFramebuffer);
    transitionToShaderRead(*m_accumFramebuffer);
    updateAccumUniformBuffer();
    updateAccumDescriptorSet();
    runAccumulationPass(*m_accumScratchFramebuffer);
    std::swap(m_accumFramebuffer, m_accumScratchFramebuffer);
  }

  transitionToShaderRead(*m_accumFramebuffer);
  updateToneMapUniformBuffer(camera);
  updateToneMapDescriptorSet();
  runToneMapPass(framebuffer);
}

void
RenderVkPT::runAccumulationPass(Framebuffer& framebuffer)
{
  VkDevice device = m_backend.logicalDevice();
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_accumRenderPass;
  framebufferInfo.attachmentCount = 1;
  VkImageView attachment = framebuffer.colorImageView();
  framebufferInfo.pAttachments = &attachment;
  framebufferInfo.width = framebuffer.width();
  framebufferInfo.height = framebuffer.height();
  framebufferInfo.layers = 1;

  VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &vkFramebuffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFramebuffer for pathtrace accumulation failed with VkResult " << result;
    m_backend.endSingleTimeCommands(commandBuffer);
    return;
  }

  VkClearValue clearValue = {};
  clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

  VkRenderPassBeginInfo renderPassBegin = {};
  renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBegin.renderPass = m_accumRenderPass;
  renderPassBegin.framebuffer = vkFramebuffer;
  renderPassBegin.renderArea.offset = { 0, 0 };
  renderPassBegin.renderArea.extent = { framebuffer.width(), framebuffer.height() };
  renderPassBegin.clearValueCount = 1;
  renderPassBegin.pClearValues = &clearValue;

  vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.width = static_cast<float>(framebuffer.width());
  viewport.height = static_cast<float>(framebuffer.height());
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {};
  scissor.extent = { framebuffer.width(), framebuffer.height() };
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_accumPipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_accumPipelineLayout,
                          0,
                          1,
                          &m_accumDescriptorSet,
                          0,
                          nullptr);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_quadVertexBuffer, &offset);
  vkCmdBindIndexBuffer(commandBuffer, m_quadIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(commandBuffer, m_quadIndexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);
  m_backend.endSingleTimeCommands(commandBuffer);
  vkDestroyFramebuffer(device, vkFramebuffer, nullptr);
}

void
RenderVkPT::runToneMapPass(Framebuffer& framebuffer)
{
  VkDevice device = m_backend.logicalDevice();
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_toneMapRenderPass;
  framebufferInfo.attachmentCount = 1;
  VkImageView attachment = framebuffer.colorImageView();
  framebufferInfo.pAttachments = &attachment;
  framebufferInfo.width = framebuffer.width();
  framebufferInfo.height = framebuffer.height();
  framebufferInfo.layers = 1;

  VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &vkFramebuffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFramebuffer for pathtrace tone map failed with VkResult " << result;
    m_backend.endSingleTimeCommands(commandBuffer);
    return;
  }

  VkClearValue clearValue = {};
  clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

  VkRenderPassBeginInfo renderPassBegin = {};
  renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBegin.renderPass = m_toneMapRenderPass;
  renderPassBegin.framebuffer = vkFramebuffer;
  renderPassBegin.renderArea.offset = { 0, 0 };
  renderPassBegin.renderArea.extent = { framebuffer.width(), framebuffer.height() };
  renderPassBegin.clearValueCount = 1;
  renderPassBegin.pClearValues = &clearValue;

  vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.width = static_cast<float>(framebuffer.width());
  viewport.height = static_cast<float>(framebuffer.height());
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {};
  scissor.extent = { framebuffer.width(), framebuffer.height() };
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMapPipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_toneMapPipelineLayout,
                          0,
                          1,
                          &m_toneMapDescriptorSet,
                          0,
                          nullptr);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_quadVertexBuffer, &offset);
  vkCmdBindIndexBuffer(commandBuffer, m_quadIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(commandBuffer, m_quadIndexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);
  m_backend.endSingleTimeCommands(commandBuffer);
  vkDestroyFramebuffer(device, vkFramebuffer, nullptr);
}

void
RenderVkPT::transitionToShaderRead(Framebuffer& framebuffer)
{
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_backend.endSingleTimeCommands(commandBuffer);
}

void
RenderVkPT::destroyFullscreenResources()
{
  m_displayFramebuffer.reset();
  m_sampleFramebuffer.reset();
  m_accumFramebuffer.reset();
  m_accumScratchFramebuffer.reset();

  destroyPipelines();

  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_toneMapDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, m_toneMapDescriptorPool, nullptr);
    m_toneMapDescriptorPool = VK_NULL_HANDLE;
    m_toneMapDescriptorSet = VK_NULL_HANDLE;
  }
  if (m_toneMapDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, m_toneMapDescriptorSetLayout, nullptr);
    m_toneMapDescriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_accumDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, m_accumDescriptorPool, nullptr);
    m_accumDescriptorPool = VK_NULL_HANDLE;
    m_accumDescriptorSet = VK_NULL_HANDLE;
  }
  if (m_accumDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, m_accumDescriptorSetLayout, nullptr);
    m_accumDescriptorSetLayout = VK_NULL_HANDLE;
  }

  if (m_framebufferSampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, m_framebufferSampler, nullptr);
    m_framebufferSampler = VK_NULL_HANDLE;
  }

  if (m_toneMapUniformBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_toneMapUniformBuffer, nullptr);
    m_toneMapUniformBuffer = VK_NULL_HANDLE;
  }
  if (m_toneMapUniformMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_toneMapUniformMemory, nullptr);
    m_toneMapUniformMemory = VK_NULL_HANDLE;
  }
  if (m_accumUniformBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_accumUniformBuffer, nullptr);
    m_accumUniformBuffer = VK_NULL_HANDLE;
  }
  if (m_accumUniformMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_accumUniformMemory, nullptr);
    m_accumUniformMemory = VK_NULL_HANDLE;
  }
  if (m_quadIndexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_quadIndexBuffer, nullptr);
    m_quadIndexBuffer = VK_NULL_HANDLE;
  }
  if (m_quadIndexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_quadIndexMemory, nullptr);
    m_quadIndexMemory = VK_NULL_HANDLE;
  }
  if (m_quadVertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_quadVertexBuffer, nullptr);
    m_quadVertexBuffer = VK_NULL_HANDLE;
  }
  if (m_quadVertexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_quadVertexMemory, nullptr);
    m_quadVertexMemory = VK_NULL_HANDLE;
  }
  m_quadIndexCount = 0;
}

void
RenderVkPT::destroyPipelines()
{
  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_toneMapPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, m_toneMapPipeline, nullptr);
    m_toneMapPipeline = VK_NULL_HANDLE;
  }
  if (m_toneMapPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, m_toneMapPipelineLayout, nullptr);
    m_toneMapPipelineLayout = VK_NULL_HANDLE;
  }
  if (m_toneMapRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_toneMapRenderPass, nullptr);
    m_toneMapRenderPass = VK_NULL_HANDLE;
  }

  if (m_accumPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, m_accumPipeline, nullptr);
    m_accumPipeline = VK_NULL_HANDLE;
  }
  if (m_accumPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, m_accumPipelineLayout, nullptr);
    m_accumPipelineLayout = VK_NULL_HANDLE;
  }
  if (m_accumRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_accumRenderPass, nullptr);
    m_accumRenderPass = VK_NULL_HANDLE;
  }
  m_toneMapPipelineColorFormat = VK_FORMAT_UNDEFINED;
}

VkShaderModule
RenderVkPT::createShaderModule(const uint32_t* words, size_t wordCount) const
{
  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = wordCount * sizeof(uint32_t);
  createInfo.pCode = words;

  VkShaderModule module = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(m_backend.logicalDevice(), &createInfo, nullptr, &module);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateShaderModule failed with VkResult " << result;
    return VK_NULL_HANDLE;
  }
  return module;
}

} // namespace gfxvulkan
