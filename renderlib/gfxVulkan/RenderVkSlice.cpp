#include "RenderVkSlice.h"

#include "CacheStatusReport.h"
#include "CCamera.h"
#include "Framebuffer.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "RenderSettings.h"
#include "SliceTransferFunction.h"
#include "SliceViewState.h"
#include "Timing.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"
#include "gfxVulkan/shadersrc/slice_frag_spv.hpp"
#include "gfxVulkan/shadersrc/slice_vert_spv.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>

namespace gfxvulkan {

namespace {

struct alignas(16) SliceUniforms
{
  glm::vec4 viewportModeChannels = glm::vec4(0.0f);
  glm::ivec4 indices = glm::ivec4(0);
  glm::ivec4 dimensions = glm::ivec4(1);
  glm::vec4 panZoomGap = glm::vec4(0.0f, 0.0f, 1.0f, 2.0f);
  glm::vec4 physicalDimensions = glm::vec4(1.0f);
  glm::vec4 flipAxes = glm::vec4(1.0f);
  glm::vec4 paneXY = glm::vec4(0.0f);
  glm::vec4 paneYZ = glm::vec4(0.0f);
  glm::vec4 paneXZ = glm::vec4(0.0f);
  glm::vec4 intensityMin = glm::vec4(0.0f);
  glm::vec4 intensityMax = glm::vec4(1.0f);
  glm::vec4 lutMin = glm::vec4(0.0f);
  glm::vec4 lutMax = glm::vec4(1.0f);
  glm::vec4 labels = glm::vec4(0.0f);
  glm::vec4 opacity = glm::vec4(0.0f);
  std::array<glm::vec4, SliceTransferFunction::MAX_CHANNELS> diffuse = {};
  glm::ivec4 tfNodeCounts = glm::ivec4(0);
  std::array<glm::vec4, SliceTransferFunction::MAX_CHANNELS * SliceTransferFunction::MAX_NODES> tfNodes = {};
};

struct SliceCameraTransform
{
  glm::vec2 pan = glm::vec2(0.0f);
  float zoom = 1.0f;
};

glm::vec4
toBottomLeftRect(const SliceRect& rect, uint32_t viewportHeight)
{
  return glm::vec4(rect.x,
                   static_cast<float>(viewportHeight) - rect.y - rect.height,
                   rect.width,
                   rect.height);
}

glm::vec2
slicePlaneWorldSize(SliceViewMode mode, const glm::vec3& boundsExtent)
{
  if (mode == SliceViewMode::X) {
    return glm::vec2(boundsExtent.z, boundsExtent.y);
  }
  if (mode == SliceViewMode::Y) {
    return glm::vec2(boundsExtent.x, boundsExtent.z);
  }
  return glm::vec2(boundsExtent.x, boundsExtent.y);
}

SliceCameraTransform
sliceCameraTransform(const Scene& scene,
                     const SliceViewState& state,
                     const CCamera& camera,
                     uint32_t viewportWidth,
                     uint32_t viewportHeight)
{
  SliceCameraTransform transform;
  if (!state.isSingleSlice() || camera.m_Projection != ORTHOGRAPHIC || viewportWidth == 0 || viewportHeight == 0) {
    return transform;
  }

  constexpr float MIN_EXTENT = 0.000001f;
  const glm::vec2 planeSize =
    glm::max(slicePlaneWorldSize(state.mode, scene.m_boundingBox.GetExtent()), glm::vec2(MIN_EXTENT));
  const float fitPixelsPerWorld =
    std::min(static_cast<float>(viewportWidth) / planeSize.x, static_cast<float>(viewportHeight) / planeSize.y);
  const float pixelsPerWorld =
    static_cast<float>(viewportHeight) / (2.0f * std::max(camera.m_OrthoScale, MIN_EXTENT));

  transform.zoom = pixelsPerWorld / fitPixelsPerWorld;
  const glm::vec3 centerOffset = scene.m_boundingBox.GetCenter() - camera.m_Target;
  transform.pan =
    glm::vec2(glm::dot(centerOffset, camera.m_U), glm::dot(centerOffset, camera.m_V)) * pixelsPerWorld;
  return transform;
}

std::optional<resources::Buffer>
createUniformBuffer(Backend& backend, size_t size)
{
  return backend.device().createBuffer(static_cast<VkDeviceSize>(size),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

std::optional<resources::UniqueRenderPass>
createSliceRenderPass(Backend& backend, VkFormat colorFormat)
{
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = colorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorReference = {};
  colorReference.attachment = 0;
  colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorReference;

  VkRenderPassCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &colorAttachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  return backend.device().createRenderPass(info);
}

} // namespace

RenderVkSlice::RenderVkSlice(Backend& backend, RenderSettings* renderSettings)
  : RenderVk(backend, renderSettings)
  , m_sliceStartTime(std::chrono::high_resolution_clock::now())
{
}

RenderVkSlice::~RenderVkSlice()
{
  destroySliceResources();
}

VolumeTextureMode
RenderVkSlice::volumeTextureMode() const
{
  return VolumeTextureMode::RawRgba16;
}

void
RenderVkSlice::initialize(uint32_t w, uint32_t h)
{
  resize(w, h);
  m_status->SetRenderBegin();
}

void
RenderVkSlice::resize(uint32_t w, uint32_t h)
{
  RenderVk::resize(w, h);
  m_internalFramebuffer.reset();
}

void
RenderVkSlice::render(const CCamera& camera)
{
  if (m_w == 0 || m_h == 0) {
    return;
  }
  if (!m_internalFramebuffer || m_internalFramebuffer->width() != m_w ||
      m_internalFramebuffer->height() != m_h) {
    gfxApi::FramebufferDesc desc;
    desc.width = m_w;
    desc.height = m_h;
    desc.colorFormat = gfxApi::FramebufferColorFormat::Rgba8;
    desc.depthStencil = false;
    m_internalFramebuffer = std::make_unique<Framebuffer>(m_backend, desc);
  }
  renderToFramebufferSlice(camera, *m_internalFramebuffer);
}

void
RenderVkSlice::renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo)
{
  auto* framebuffer = dynamic_cast<Framebuffer*>(fbo);
  if (!framebuffer) {
    LOG_ERROR << "gfxvulkan::RenderVkSlice::renderTo requires a Vulkan framebuffer";
    return;
  }
  renderToFramebufferSlice(camera, *framebuffer);
}

gfxApi::ClearColor
RenderVkSlice::backgroundClearColor() const
{
  if (!m_scene) {
    return {};
  }
  return { m_scene->m_material.m_backgroundColor[0],
           m_scene->m_material.m_backgroundColor[1],
           m_scene->m_material.m_backgroundColor[2],
           1.0f };
}

void
RenderVkSlice::renderToFramebufferSlice(const CCamera& camera, Framebuffer& framebuffer)
{
  if (!prepareToRender()) {
    framebuffer.clear(backgroundClearColor());
    return;
  }
  if (!ensureResources(framebuffer.colorFormat()) ||
      !updateUniformBuffer(camera, framebuffer.width(), framebuffer.height()) || !updateDescriptorSet()) {
    framebuffer.clear(backgroundClearColor());
    return;
  }

  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkImageView attachment = framebuffer.colorImageView();
  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_sliceRenderPass.get();
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.pAttachments = &attachment;
  framebufferInfo.width = framebuffer.width();
  framebufferInfo.height = framebuffer.height();
  framebufferInfo.layers = 1;
  auto vkFramebuffer = m_backend.device().createFramebuffer(framebufferInfo);
  if (!vkFramebuffer) {
    m_backend.endSingleTimeCommands(commandBuffer);
    return;
  }

  VkRenderPassBeginInfo renderPassBegin = {};
  renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBegin.renderPass = m_sliceRenderPass.get();
  renderPassBegin.framebuffer = vkFramebuffer->get();
  renderPassBegin.renderArea.extent = { framebuffer.width(), framebuffer.height() };
  vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.width = static_cast<float>(framebuffer.width());
  viewport.height = static_cast<float>(framebuffer.height());
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {};
  scissor.extent = { framebuffer.width(), framebuffer.height() };
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_slicePipeline.get());
  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_slicePipelineLayout.get(),
                          0,
                          1,
                          &m_sliceDescriptorSet,
                          0,
                          nullptr);
  vkCmdDraw(commandBuffer, 4, 1, 0, 0);
  vkCmdEndRenderPass(commandBuffer);
  m_backend.endSingleTimeCommands(commandBuffer);

  const auto endTime = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double> elapsed = endTime - m_sliceStartTime;
  m_status->SetStatisticChanged("Performance", "Render Image", formatDurationMs(elapsed.count() * 1000.0), "ms.");
  reportCacheStatistics(m_status.get());
  m_sliceStartTime = endTime;
}

bool
RenderVkSlice::ensureResources(VkFormat colorFormat)
{
  VkDevice device = m_backend.logicalDevice();
  if (!m_sliceUniformBuffer) {
    auto buffer = createUniformBuffer(m_backend, sizeof(SliceUniforms));
    if (!buffer) {
      return false;
    }
    m_sliceUniformBuffer = std::move(*buffer);
  }

  if (!m_sliceDescriptorSetLayout) {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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
    auto layout = m_backend.device().createDescriptorSetLayout(layoutInfo);
    if (!layout) {
      return false;
    }
    m_sliceDescriptorSetLayout = std::move(*layout);
  }

  if (!m_sliceDescriptorPool) {
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
    auto pool = m_backend.device().createDescriptorPool(poolInfo);
    if (!pool) {
      return false;
    }
    m_sliceDescriptorPool = std::move(*pool);
  }

  if (m_sliceDescriptorSet == VK_NULL_HANDLE) {
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_sliceDescriptorPool.get();
    allocateInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorLayout = m_sliceDescriptorSetLayout.get();
    allocateInfo.pSetLayouts = &descriptorLayout;
    if (vkAllocateDescriptorSets(device, &allocateInfo, &m_sliceDescriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets failed for slice renderer";
      return false;
    }
  }

  if (m_slicePipeline && m_slicePipelineColorFormat == colorFormat) {
    return true;
  }
  destroyPipeline();
  m_slicePipelineColorFormat = colorFormat;

  auto renderPass = createSliceRenderPass(m_backend, colorFormat);
  if (!renderPass) {
    return false;
  }
  m_sliceRenderPass = std::move(*renderPass);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  VkDescriptorSetLayout descriptorLayout = m_sliceDescriptorSetLayout.get();
  pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
  auto pipelineLayout = m_backend.device().createPipelineLayout(pipelineLayoutInfo);
  if (!pipelineLayout) {
    return false;
  }
  m_slicePipelineLayout = std::move(*pipelineLayout);

  auto vertexShader = m_backend.device().createShaderModule(slice_vert_spv, slice_vert_spv_word_count);
  auto fragmentShader = m_backend.device().createShaderModule(slice_frag_spv, slice_frag_spv_word_count);
  if (!vertexShader || !fragmentShader) {
    return false;
  }
  std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertexShader->get();
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragmentShader->get();
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertexInput = {};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment = {};
  blendAttachment.blendEnable = VK_TRUE;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo colorBlend = {};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &blendAttachment;

  std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamic = {};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.pDynamicState = &dynamic;
  pipelineInfo.layout = m_slicePipelineLayout.get();
  pipelineInfo.renderPass = m_sliceRenderPass.get();
  auto pipeline = m_backend.device().createPipeline(pipelineInfo);
  if (!pipeline) {
    return false;
  }
  m_slicePipeline = std::move(*pipeline);
  return true;
}

bool
RenderVkSlice::updateUniformBuffer(const CCamera& camera, uint32_t viewportWidth, uint32_t viewportHeight)
{
  if (!m_scene || !m_scene->m_volume || !m_renderSettings || !m_sliceUniformBuffer) {
    return false;
  }

  ImageXYZC* volume = m_scene->m_volume.get();
  const SliceTransferFunction transfer = buildSliceTransferFunction(*m_scene);
  const glm::ivec3 dimensions(static_cast<int>(volume->sizeX()),
                              static_cast<int>(volume->sizeY()),
                              static_cast<int>(volume->sizeZ()));
  const glm::ivec3 indices =
    glm::clamp(m_renderSettings->m_SliceView.indices, glm::ivec3(0), dimensions - glm::ivec3(1));
  const glm::vec3 physicalDimensions = glm::max(volume->getPhysicalDimensions(), glm::vec3(0.000001f));
  const TripleSliceLayout layout =
    computeTripleSliceLayout(physicalDimensions, glm::ivec2(viewportWidth, viewportHeight), 2.0f);

  SliceUniforms uniforms;
  uniforms.viewportModeChannels = glm::vec4(static_cast<float>(viewportWidth),
                                             static_cast<float>(viewportHeight),
                                             static_cast<float>(m_renderSettings->m_SliceView.mode),
                                             static_cast<float>(transfer.channelCount));
  uniforms.indices = glm::ivec4(indices, 0);
  uniforms.dimensions = glm::ivec4(dimensions, 1);
  const SliceViewState& sliceView = m_renderSettings->m_SliceView;
  const SliceCameraTransform cameraTransform =
    sliceCameraTransform(*m_scene, sliceView, camera, viewportWidth, viewportHeight);
  uniforms.panZoomGap = glm::vec4(cameraTransform.pan, cameraTransform.zoom, 2.0f);
  uniforms.physicalDimensions = glm::vec4(physicalDimensions, 0.0f);
  uniforms.flipAxes = glm::vec4(glm::vec3(volume->getVolumeAxesFlipped()), 0.0f);
  uniforms.paneXY = toBottomLeftRect(layout.xy, viewportHeight);
  uniforms.paneYZ = toBottomLeftRect(layout.yz, viewportHeight);
  uniforms.paneXZ = toBottomLeftRect(layout.xz, viewportHeight);
  uniforms.intensityMin = transfer.intensityMin;
  uniforms.intensityMax = transfer.intensityMax;
  uniforms.lutMin = transfer.lutMin;
  uniforms.lutMax = transfer.lutMax;
  uniforms.labels = transfer.labels;
  uniforms.opacity = transfer.opacity;
  uniforms.diffuse = transfer.diffuse;
  uniforms.tfNodeCounts = transfer.nodeCounts;
  uniforms.tfNodes = transfer.nodes;

  void* mapped = nullptr;
  if (vkMapMemory(
        m_backend.logicalDevice(), m_sliceUniformBuffer.memory(), 0, sizeof(SliceUniforms), 0, &mapped) != VK_SUCCESS) {
    return false;
  }
  std::memcpy(mapped, &uniforms, sizeof(uniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_sliceUniformBuffer.memory());
  return true;
}

bool
RenderVkSlice::updateDescriptorSet()
{
  const VolumeTextureVk& volume = volumeTexture();
  if (!volume.valid()) {
    return false;
  }
  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = m_sliceUniformBuffer.get();
  bufferInfo.range = sizeof(SliceUniforms);
  VkDescriptorImageInfo volumeInfo = {};
  volumeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  volumeInfo.imageView = volume.volumeView();
  volumeInfo.sampler = volume.volumeSampler();
  VkDescriptorImageInfo transferInfo = {};
  transferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  transferInfo.imageView = volume.transferView();
  transferInfo.sampler = volume.transferSampler();

  std::array<VkWriteDescriptorSet, 3> writes = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = m_sliceDescriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufferInfo;
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = m_sliceDescriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].descriptorCount = 1;
  writes[1].pImageInfo = &volumeInfo;
  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = m_sliceDescriptorSet;
  writes[2].dstBinding = 2;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[2].descriptorCount = 1;
  writes[2].pImageInfo = &transferInfo;
  vkUpdateDescriptorSets(
    m_backend.logicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return true;
}

void
RenderVkSlice::destroyPipeline()
{
  m_slicePipeline.reset();
  m_slicePipelineLayout.reset();
  m_sliceRenderPass.reset();
  m_slicePipelineColorFormat = VK_FORMAT_UNDEFINED;
}

void
RenderVkSlice::destroySliceResources()
{
  m_internalFramebuffer.reset();
  destroyPipeline();
  m_sliceDescriptorPool.reset();
  m_sliceDescriptorSet = VK_NULL_HANDLE;
  m_sliceDescriptorSetLayout.reset();
  m_sliceUniformBuffer.reset();
}

void
RenderVkSlice::cleanUpResources()
{
  destroySliceResources();
  RenderVk::cleanUpResources();
}

} // namespace gfxvulkan
