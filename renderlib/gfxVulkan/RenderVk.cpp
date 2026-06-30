#include "RenderVk.h"

#include "CCamera.h"
#include "Enumerations.h"
#include "Framebuffer.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "MathUtil.h"
#include "RenderSettings.h"
#include "ScenePlane.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/shadersrc/volume_frag_spv.hpp"
#include "gfxVulkan/shadersrc/volume_vert_spv.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace gfxvulkan {

namespace {

struct alignas(16) VolumeUniforms
{
  glm::mat4 modelViewMatrix = glm::mat4(1.0f);
  glm::mat4 projectionMatrix = glm::mat4(1.0f);
  glm::mat4 inverseModelViewMatrix = glm::mat4(1.0f);
  glm::vec4 clipPlane = glm::vec4(0.0f);
  glm::vec4 aabbMinMode = glm::vec4(-0.5f, -0.5f, -0.5f, 0.0f);
  glm::vec4 aabbMaxSteps = glm::vec4(0.5f, 0.5f, 0.5f, 512.0f);
  glm::vec4 flipAxesPerspective = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  glm::vec4 viewportDensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
  glm::vec4 colorParams = glm::vec4(1.0f, 0.0f, 1.0f, 1.3657f);
  glm::vec4 lutMin = glm::vec4(0.0f);
  glm::vec4 lutMax = glm::vec4(1.0f);
  glm::vec4 background = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
};

const std::array<float, 24> kCubeVertices = {
  -0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,
  -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
};

const std::array<uint16_t, 36> kCubeIndices = {
  0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
  4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3,
};

glm::mat4
vulkanProjectionCorrection()
{
  glm::mat4 correction(1.0f);
  correction[1][1] = -1.0f;
  correction[2][2] = 0.5f;
  correction[3][2] = 0.5f;
  return correction;
}

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

} // namespace

const std::string RenderVk::TYPE_NAME = "raymarch";

RenderVk::RenderVk(Backend& backend, RenderSettings* renderSettings)
  : m_backend(backend)
  , m_volume(backend)
  , m_renderSettings(renderSettings)
  , m_status(new CStatus)
{
  m_startTime = std::chrono::high_resolution_clock::now();
}

RenderVk::~RenderVk()
{
  cleanUpResources();
}

VolumeTextureMode
RenderVk::volumeTextureMode() const
{
  return VolumeTextureMode::FusedRgba8;
}

bool
RenderVk::usesProgressiveAccumulation() const
{
  return false;
}

float
RenderVk::volumeShaderMode() const
{
  return 0.0f;
}

float
RenderVk::rayStepCount() const
{
  return 512.0f;
}

void
RenderVk::initialize(uint32_t w, uint32_t h)
{
  resize(w, h);
  ensureFrameResources();
  m_status->SetRenderBegin();
}

void
RenderVk::resize(uint32_t w, uint32_t h)
{
  m_w = w;
  m_h = h;
  m_internalFramebuffer.reset();
  if (m_renderSettings) {
    m_renderSettings->SetNoIterations(0);
  }
}

void
RenderVk::getSize(uint32_t& w, uint32_t& h)
{
  w = m_w;
  h = m_h;
}

bool
RenderVk::prepareToRender()
{
  if (!m_scene || !m_scene->m_volume || !m_renderSettings) {
    return false;
  }

  const long volumeDirtyFlags = VolumeDirty | VolumeDataDirty | TransferFunctionDirty | RenderParamsDirty;
  if (!m_volume.valid() || m_renderSettings->m_DirtyFlags.HasFlag(volumeDirtyFlags)) {
    if (!m_volume.upload(*m_scene, volumeTextureMode(), m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling)) {
      return false;
    }
    m_status->SetRenderBegin();
  }

  if (usesProgressiveAccumulation() &&
      m_renderSettings->m_DirtyFlags.HasFlag(CameraDirty | LightsDirty | RenderParamsDirty | TransferFunctionDirty |
                                             RoiDirty | VolumeDataDirty | FilmResolutionDirty)) {
    m_renderSettings->SetNoIterations(0);
  }

  m_renderSettings->m_RenderSettings.m_GradientDelta =
    1.0f / static_cast<float>(std::max<size_t>(1, m_scene->m_volume->maxPixelDimension()));
  m_renderSettings->m_DirtyFlags.ClearAllFlags();
  return true;
}

void
RenderVk::render(const CCamera& camera)
{
  if (m_w == 0 || m_h == 0) {
    return;
  }

  if (!m_internalFramebuffer || m_internalFramebuffer->width() != m_w || m_internalFramebuffer->height() != m_h) {
    gfxApi::FramebufferDesc desc;
    desc.width = m_w;
    desc.height = m_h;
    desc.colorFormat = gfxApi::FramebufferColorFormat::Rgba8;
    desc.depthStencil = false;
    m_internalFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);
  }

  renderToFramebuffer(camera, *m_internalFramebuffer);
}

void
RenderVk::renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo)
{
  auto* vkFramebuffer = dynamic_cast<Framebuffer*>(fbo);
  if (!vkFramebuffer) {
    LOG_ERROR << "gfxvulkan::RenderVk::renderTo requires a Vulkan framebuffer";
    return;
  }
  renderToFramebuffer(camera, *vkFramebuffer);
}

void
RenderVk::renderToFramebuffer(const CCamera& camera, Framebuffer& framebuffer)
{
  if (!prepareToRender()) {
    framebuffer.clear(backgroundClearColor());
    return;
  }

  if (!ensureFrameResources() || !ensurePipeline(framebuffer.colorFormat()) || !updateUniformBuffer(camera) ||
      !updateDescriptorSet()) {
    framebuffer.clear(backgroundClearColor());
    return;
  }

  VkDevice device = m_backend.logicalDevice();
  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_renderPass;
  framebufferInfo.attachmentCount = 1;
  VkImageView attachment = framebuffer.colorImageView();
  framebufferInfo.pAttachments = &attachment;
  framebufferInfo.width = framebuffer.width();
  framebufferInfo.height = framebuffer.height();
  framebufferInfo.layers = 1;

  VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &vkFramebuffer);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateFramebuffer failed with VkResult " << result;
    m_backend.endSingleTimeCommands(commandBuffer);
    return;
  }

  const gfxApi::ClearColor bg = backgroundClearColor();
  VkClearValue clearValue = {};
  clearValue.color = { { bg.r, bg.g, bg.b, bg.a } };

  VkRenderPassBeginInfo renderPassBegin = {};
  renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBegin.renderPass = m_renderPass;
  renderPassBegin.framebuffer = vkFramebuffer;
  renderPassBegin.renderArea.offset = { 0, 0 };
  renderPassBegin.renderArea.extent = { framebuffer.width(), framebuffer.height() };
  renderPassBegin.clearValueCount = 1;
  renderPassBegin.pClearValues = &clearValue;

  vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(framebuffer.width());
  viewport.height = static_cast<float>(framebuffer.height());
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {};
  scissor.offset = { 0, 0 };
  scissor.extent = { framebuffer.width(), framebuffer.height() };

  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipelineLayout,
                          0,
                          1,
                          &m_descriptorSet,
                          0,
                          nullptr);

  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, &offset);
  vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);
  m_backend.endSingleTimeCommands(commandBuffer);
  vkDestroyFramebuffer(device, vkFramebuffer, nullptr);

  if (usesProgressiveAccumulation()) {
    m_renderSettings->SetNoIterations(m_renderSettings->GetNoIterations() + 1);
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = endTime - m_startTime;
  m_status->SetStatisticChanged("Performance", "Render Image", std::to_string(elapsed.count() * 1000.0), "ms.");
  if (usesProgressiveAccumulation()) {
    m_status->SetStatisticChanged("Performance", "No. Iterations", std::to_string(m_renderSettings->GetNoIterations()));
  }
  m_startTime = std::chrono::high_resolution_clock::now();
}

bool
RenderVk::ensureFrameResources()
{
  if (m_vertexBuffer != VK_NULL_HANDLE && m_indexBuffer != VK_NULL_HANDLE && m_uniformBuffer != VK_NULL_HANDLE) {
    return true;
  }

  destroyFrameResources();

  if (!uploadHostBuffer(m_backend,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        kCubeVertices.data(),
                        kCubeVertices.size(),
                        m_vertexBuffer,
                        m_vertexMemory)) {
    return false;
  }

  if (!uploadHostBuffer(m_backend,
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        kCubeIndices.data(),
                        kCubeIndices.size(),
                        m_indexBuffer,
                        m_indexMemory)) {
    return false;
  }

  if (!createBuffer(m_backend,
                    sizeof(VolumeUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_uniformBuffer,
                    m_uniformMemory)) {
    return false;
  }

  m_indexCount = static_cast<uint32_t>(kCubeIndices.size());
  return true;
}

VkShaderModule
RenderVk::createShaderModule(const uint32_t* words, size_t wordCount) const
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

bool
RenderVk::ensurePipeline(VkFormat colorFormat)
{
  if (m_pipeline != VK_NULL_HANDLE && m_pipelineColorFormat == colorFormat) {
    return true;
  }

  destroyPipeline();
  VkDevice device = m_backend.logicalDevice();
  m_pipelineColorFormat = colorFormat;

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

  VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {};
  descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  descriptorLayoutInfo.pBindings = bindings.data();
  VkResult result = vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &m_descriptorSetLayout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateDescriptorSetLayout failed with VkResult " << result;
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
  result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateDescriptorPool failed with VkResult " << result;
    return false;
  }

  VkDescriptorSetAllocateInfo descriptorSetInfo = {};
  descriptorSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetInfo.descriptorPool = m_descriptorPool;
  descriptorSetInfo.descriptorSetCount = 1;
  descriptorSetInfo.pSetLayouts = &m_descriptorSetLayout;
  result = vkAllocateDescriptorSets(device, &descriptorSetInfo, &m_descriptorSet);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkAllocateDescriptorSets failed with VkResult " << result;
    return false;
  }

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
  result = vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateRenderPass failed with VkResult " << result;
    return false;
  }

  VkShaderModule vertexShader = createShaderModule(volume_vert_spv, volume_vert_spv_word_count);
  VkShaderModule fragmentShader = createShaderModule(volume_frag_spv, volume_frag_spv_word_count);
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
  bindingDescription.stride = sizeof(float) * 3;
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributeDescription = {};
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescription.offset = 0;

  VkPipelineVertexInputStateCreateInfo vertexInput = {};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &bindingDescription;
  vertexInput.vertexAttributeDescriptionCount = 1;
  vertexInput.pVertexAttributeDescriptions = &attributeDescription;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
  result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreatePipelineLayout failed with VkResult " << result;
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    return false;
  }

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
  pipelineInfo.layout = m_pipelineLayout;
  pipelineInfo.renderPass = m_renderPass;
  pipelineInfo.subpass = 0;

  result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
  vkDestroyShaderModule(device, fragmentShader, nullptr);
  vkDestroyShaderModule(device, vertexShader, nullptr);
  if (result != VK_SUCCESS) {
    LOG_ERROR << "vkCreateGraphicsPipelines failed with VkResult " << result;
    return false;
  }

  return true;
}

bool
RenderVk::updateDescriptorSet()
{
  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = m_uniformBuffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(VolumeUniforms);

  VkDescriptorImageInfo volumeInfo = {};
  volumeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  volumeInfo.imageView = m_volume.volumeView();
  volumeInfo.sampler = m_volume.volumeSampler();

  VkDescriptorImageInfo transferInfo = {};
  transferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  transferInfo.imageView = m_volume.transferView();
  transferInfo.sampler = m_volume.transferSampler();

  std::array<VkWriteDescriptorSet, 3> writes = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = m_descriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufferInfo;
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = m_descriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].descriptorCount = 1;
  writes[1].pImageInfo = &volumeInfo;
  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = m_descriptorSet;
  writes[2].dstBinding = 2;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[2].descriptorCount = 1;
  writes[2].pImageInfo = &transferInfo;

  vkUpdateDescriptorSets(m_backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return true;
}

bool
RenderVk::updateUniformBuffer(const CCamera& camera)
{
  if (!m_scene || !m_scene->m_volume || m_uniformMemory == VK_NULL_HANDLE) {
    return false;
  }

  glm::vec3 dims(m_scene->m_volume->sizeX() * m_scene->m_volume->physicalSizeX(),
                 m_scene->m_volume->sizeY() * m_scene->m_volume->physicalSizeY(),
                 m_scene->m_volume->sizeZ() * m_scene->m_volume->physicalSizeZ());
  float maxd = std::max(dims.x, std::max(dims.y, dims.z));
  if (maxd <= 0.0f) {
    maxd = 1.0f;
  }
  glm::vec3 scales(dims.x / maxd, dims.y / maxd, dims.z / maxd);
  glm::mat4 modelMatrix = glm::scale(glm::mat4(1.0f), scales);
  modelMatrix = glm::translate(modelMatrix, glm::vec3(0.5f));

  glm::mat4 viewMatrix(1.0f);
  glm::mat4 projectionMatrix(1.0f);
  camera.getViewMatrix(viewMatrix);
  camera.getProjMatrix(projectionMatrix);
  projectionMatrix = vulkanProjectionCorrection() * projectionMatrix;

  VolumeUniforms uniforms;
  uniforms.modelViewMatrix = viewMatrix * modelMatrix;
  uniforms.projectionMatrix = projectionMatrix;
  uniforms.inverseModelViewMatrix = glm::inverse(uniforms.modelViewMatrix);
  if (m_scene->m_clipPlane && m_scene->m_clipPlane->m_enabled) {
    Plane plane = Plane().transform(m_scene->m_clipPlane->m_transform.getMatrix());
    plane = plane.transform(glm::inverse(modelMatrix));
    uniforms.clipPlane = plane.asVec4();
  }

  uniforms.aabbMinMode = glm::vec4(m_scene->m_roi.GetMinP() - glm::vec3(0.5f), volumeShaderMode());
  uniforms.aabbMaxSteps = glm::vec4(m_scene->m_roi.GetMaxP() - glm::vec3(0.5f), rayStepCount());

  const glm::ivec3 flip = m_scene->m_volume->getVolumeAxesFlipped();
  uniforms.flipAxesPerspective =
    glm::vec4(static_cast<float>(flip.x),
              static_cast<float>(flip.y),
              static_cast<float>(flip.z),
              camera.m_Projection == PERSPECTIVE ? 1.0f : 0.0f);
  uniforms.viewportDensity =
    glm::vec4(static_cast<float>(std::max<uint32_t>(1, m_w)),
              static_cast<float>(std::max<uint32_t>(1, m_h)),
              camera.m_OrthoScale,
              m_renderSettings->m_RenderSettings.m_DensityScale / 100.0f);
  const float iterationSeed =
    usesProgressiveAccumulation() ? static_cast<float>(m_renderSettings->GetNoIterations() + 1) : 0.0f;
  uniforms.colorParams =
    glm::vec4((1.0f - camera.m_Film.m_Exposure) + 1.0f, iterationSeed, 1.0f, 1.3657f);
  uniforms.lutMin = m_volume.lutMin();
  uniforms.lutMax = m_volume.lutMax();
  uniforms.background = glm::vec4(m_scene->m_material.m_backgroundColor[0],
                                  m_scene->m_material.m_backgroundColor[1],
                                  m_scene->m_material.m_backgroundColor[2],
                                  1.0f);

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), m_uniformMemory, 0, sizeof(VolumeUniforms), 0, &mapped);
  std::memcpy(mapped, &uniforms, sizeof(VolumeUniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_uniformMemory);
  return true;
}

void
RenderVk::cleanUpResources()
{
  m_internalFramebuffer.reset();
  destroyPipeline();
  destroyFrameResources();
  m_volume.release();
}

void
RenderVk::destroyFrameResources()
{
  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_uniformBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_uniformBuffer, nullptr);
    m_uniformBuffer = VK_NULL_HANDLE;
  }
  if (m_uniformMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_uniformMemory, nullptr);
    m_uniformMemory = VK_NULL_HANDLE;
  }
  if (m_indexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_indexBuffer, nullptr);
    m_indexBuffer = VK_NULL_HANDLE;
  }
  if (m_indexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_indexMemory, nullptr);
    m_indexMemory = VK_NULL_HANDLE;
  }
  if (m_vertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    m_vertexBuffer = VK_NULL_HANDLE;
  }
  if (m_vertexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_vertexMemory, nullptr);
    m_vertexMemory = VK_NULL_HANDLE;
  }
  m_indexCount = 0;
}

void
RenderVk::destroyPipeline()
{
  VkDevice device = m_backend.logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }

  if (m_pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
  }
  if (m_pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
  }
  if (m_renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
  }
  if (m_descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSet = VK_NULL_HANDLE;
  }
  if (m_descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    m_descriptorSetLayout = VK_NULL_HANDLE;
  }
  m_pipelineColorFormat = VK_FORMAT_UNDEFINED;
}

RenderSettings&
RenderVk::renderSettings()
{
  return *m_renderSettings;
}

Scene*
RenderVk::scene()
{
  return m_scene;
}

void
RenderVk::setScene(Scene* s)
{
  if (m_scene != s) {
    m_volume.release();
  }
  m_scene = s;
}

gfxApi::ClearColor
RenderVk::backgroundClearColor() const
{
  if (!m_scene) {
    return {};
  }
  return { m_scene->m_material.m_backgroundColor[0],
           m_scene->m_material.m_backgroundColor[1],
           m_scene->m_material.m_backgroundColor[2],
           1.0f };
}

} // namespace gfxvulkan
