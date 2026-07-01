#include "GestureRenderer.h"

#include "CCamera.h"
#include "Framebuffer.h"
#include "Logging.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxapi/Backend.h"
#include "gfxapi/Framebuffer.h"
#include "renderlib.h"

#include "gfxVulkan/shadersrc/gui_frag_spv.hpp"
#include "gfxVulkan/shadersrc/gui_vert_spv.hpp"
#include "gfxVulkan/shadersrc/thickLines_frag_spv.hpp"
#include "gfxVulkan/shadersrc/thickLines_vert_spv.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace gfxvulkan {

namespace {

struct alignas(16) GuiParams
{
  glm::mat4 projection = glm::mat4(1.0f);
  int picking = 0;
  int pad[3] = {};
};

// Matches the layout of ThickLinesParams in thickLines.vert / thickLines.frag.
// std140: mat4 at 0..63, vec2 at 64..71, three trailing ints/float at 72..83.
struct alignas(16) ThickLinesParams
{
  glm::mat4 projection = glm::mat4(1.0f);
  glm::vec2 resolution = glm::vec2(1.0f);
  int stripVertexOffset = 0;
  int picking = 0;
  float thickness = 1.0f;
  float pad = 0.0f;
};

// Vulkan clip space differs from OpenGL (inverted Y, depth 0..1). The volume
// renderer applies the same correction, so the gizmo overlay must too in order
// to line up with the rendered scene.
glm::mat4
vulkanProjectionCorrection()
{
  glm::mat4 c(1.0f);
  c[1][1] = -1.0f;
  c[2][2] = 0.5f;
  c[3][2] = 0.5f;
  return c;
}

uint32_t
selectionRGB8ToCode(const uint8_t* rgba)
{
  uint32_t code = (uint32_t(rgba[0]) << 0) | (uint32_t(rgba[1]) << 8) | (uint32_t(rgba[2]) << 16);
  return code == 0xffffff ? Gesture::Graphics::k_noSelectionCode : code;
}

VkPrimitiveTopology
vkTopology(int t)
{
  switch (t) {
    case GestureRenderer::kLine:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case GestureRenderer::kPoint:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    default:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }
}

int
topologyForCommand(Gesture::Graphics::PrimitiveType p)
{
  switch (p) {
    case Gesture::Graphics::PrimitiveType::kLines:
      return GestureRenderer::kLine;
    case Gesture::Graphics::PrimitiveType::kPoints:
      return GestureRenderer::kPoint;
    default:
      return GestureRenderer::kTri;
  }
}

bool
createColorRenderPass(Backend& backend, VkFormat colorFormat, VkRenderPass& renderPass)
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

  VkAttachmentReference ref = {};
  ref.attachment = 0;
  ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &ref;

  VkRenderPassCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &colorAttachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;

  if (vkCreateRenderPass(backend.logicalDevice(), &info, nullptr, &renderPass) != VK_SUCCESS) {
    LOG_ERROR << "vkCreateRenderPass for gesture pass failed";
    return false;
  }
  return true;
}

} // namespace

GestureRenderer::GestureRenderer() {}

GestureRenderer::~GestureRenderer()
{
  destroy();
}

bool
GestureRenderer::ensureBackend()
{
  if (m_backend) {
    return true;
  }
  gfxApi::Backend* backend = renderlib::graphicsBackend();
  if (!backend || backend->kind() != gfxApi::BackendKind::Vulkan) {
    return false;
  }
  m_backend = static_cast<Backend*>(backend);
  return m_backend->isValid();
}

void
GestureRenderer::setTargetFramebuffer(gfxApi::Framebuffer* target)
{
  m_target = target;
}

bool
GestureRenderer::selectionBufferMatches(int width, int height) const
{
  return width == m_selectionWidth && height == m_selectionHeight;
}

bool
GestureRenderer::updateSelectionBuffer(int width, int height)
{
  return ensureSelectionFramebuffer(width, height);
}

void
GestureRenderer::clearSelectionBuffer()
{
  m_selectionWidth = 0;
  m_selectionHeight = 0;
  m_selectionFbo.reset();
}

VkShaderModule
GestureRenderer::createShaderModule(const uint32_t* words, size_t wordCount) const
{
  VkShaderModuleCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = wordCount * sizeof(uint32_t);
  info.pCode = words;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(m_backend->logicalDevice(), &info, nullptr, &module) != VK_SUCCESS) {
    LOG_ERROR << "vkCreateShaderModule for gesture shader failed";
    return VK_NULL_HANDLE;
  }
  return module;
}

bool
GestureRenderer::ensureCommonResources()
{
  VkDevice device = m_backend->logicalDevice();

  if (m_uniformBuffer == VK_NULL_HANDLE &&
      !createBuffer(*m_backend,
                    sizeof(GuiParams),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_uniformBuffer,
                    m_uniformMemory)) {
    return false;
  }

  // 1x1 white placeholder for the gui Texture binding. Gizmo verts flag
  // "no texture" via uv < -64, so this is never actually sampled for them.
  if (m_dummyView == VK_NULL_HANDLE) {
    if (!createImage(*m_backend,
                     1,
                     1,
                     1,
                     1,
                     VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_TYPE_2D,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     m_dummyImage,
                     m_dummyMemory) ||
        !createImageView(*m_backend,
                         m_dummyImage,
                         VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_VIEW_TYPE_2D,
                         VK_IMAGE_ASPECT_COLOR_BIT,
                         1,
                         m_dummyView)) {
      return false;
    }
    transitionImageLayout(*m_backend,
                          m_dummyImage,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          1);
    VkSamplerCreateInfo s = {};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_NEAREST;
    s.minFilter = VK_FILTER_NEAREST;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &s, nullptr, &m_dummySampler) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateSampler for gesture dummy texture failed";
      return false;
    }
  }

  if (m_descriptorSetLayout == VK_NULL_HANDLE) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(bindings.size());
    li.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorSetLayout for gesture failed";
      return false;
    }

    std::array<VkDescriptorPoolSize, 2> ps = {};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(device, &pi, nullptr, &m_descriptorPool) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorPool for gesture failed";
      return false;
    }
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &ai, &m_descriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for gesture failed";
      return false;
    }

    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = m_uniformBuffer;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(GuiParams);
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = m_dummyView;
    imgInfo.sampler = m_dummySampler;
    std::array<VkWriteDescriptorSet, 2> writes = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &bufInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  if (m_pipelineLayout == VK_NULL_HANDLE) {
    VkPipelineLayoutCreateInfo pli = {};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_descriptorSetLayout;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
      LOG_ERROR << "vkCreatePipelineLayout for gesture failed";
      return false;
    }
  }
  return true;
}

bool
GestureRenderer::ensureSelectionFramebuffer(int width, int height)
{
  if (!ensureBackend()) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (m_selectionFbo && m_selectionWidth == width && m_selectionHeight == height) {
    return true;
  }
  gfxApi::FramebufferDesc desc;
  desc.width = static_cast<uint32_t>(width);
  desc.height = static_cast<uint32_t>(height);
  desc.colorFormat = gfxApi::FramebufferColorFormat::Rgba8;
  desc.depthStencil = false;
  m_selectionFbo = std::make_unique<Framebuffer>(*m_backend, desc);
  m_selectionWidth = width;
  m_selectionHeight = height;

  // The selection render pass format must match the selection framebuffer.
  if (m_selectionRenderPass == VK_NULL_HANDLE &&
      !createColorRenderPass(*m_backend, m_selectionFbo->colorFormat(), m_selectionRenderPass)) {
    return false;
  }
  return true;
}

VkPipeline
GestureRenderer::createPipeline(VkRenderPass renderPass, Topology topology)
{
  VkDevice device = m_backend->logicalDevice();
  const bool blendEnable = (renderPass == m_displayRenderPass);

  VkShaderModule vs = createShaderModule(gui_vert_spv, gui_vert_spv_word_count);
  VkShaderModule fs = createShaderModule(gui_frag_spv, gui_frag_spv_word_count);
  if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
    if (vs)
      vkDestroyShaderModule(device, vs, nullptr);
    if (fs)
      vkDestroyShaderModule(device, fs, nullptr);
    return VK_NULL_HANDLE;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding = {};
  binding.binding = 0;
  binding.stride = sizeof(Gesture::Graphics::VertsCode);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 4> attrs = {};
  attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Gesture::Graphics::VertsCode, x) };
  attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Gesture::Graphics::VertsCode, u) };
  attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Gesture::Graphics::VertsCode, r) };
  attrs[3] = { 3, 0, VK_FORMAT_R32_UINT, offsetof(Gesture::Graphics::VertsCode, s) };

  VkPipelineVertexInputStateCreateInfo vi = {};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vi.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo ia = {};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = vkTopology(topology);

  VkPipelineViewportStateCreateInfo vp = {};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs = {};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms = {};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba = {};
  cba.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  cba.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo cb = {};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo ds = {};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  ds.dynamicStateCount = static_cast<uint32_t>(dyn.size());
  ds.pDynamicStates = dyn.data();

  VkGraphicsPipelineCreateInfo pi = {};
  pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vi;
  pi.pInputAssemblyState = &ia;
  pi.pViewportState = &vp;
  pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms;
  pi.pColorBlendState = &cb;
  pi.pDynamicState = &ds;
  pi.layout = m_pipelineLayout;
  pi.renderPass = renderPass;
  pi.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline);
  vkDestroyShaderModule(device, fs, nullptr);
  vkDestroyShaderModule(device, vs, nullptr);
  if (r != VK_SUCCESS) {
    LOG_ERROR << "vkCreateGraphicsPipelines for gesture failed with VkResult " << r;
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

bool
GestureRenderer::ensureDisplayPipelines(VkFormat colorFormat)
{
  if (m_displayRenderPass != VK_NULL_HANDLE && m_displayColorFormat == colorFormat &&
      m_displayPipelines[0] != VK_NULL_HANDLE) {
    return true;
  }
  VkDevice device = m_backend->logicalDevice();
  for (auto& p : m_displayPipelines) {
    if (p != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, p, nullptr);
      p = VK_NULL_HANDLE;
    }
  }
  if (m_displayRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_displayRenderPass, nullptr);
    m_displayRenderPass = VK_NULL_HANDLE;
  }
  if (!createColorRenderPass(*m_backend, colorFormat, m_displayRenderPass)) {
    return false;
  }
  m_displayColorFormat = colorFormat;
  for (int t = 0; t < kTopologyCount; ++t) {
    m_displayPipelines[t] = createPipeline(m_displayRenderPass, static_cast<Topology>(t));
    if (m_displayPipelines[t] == VK_NULL_HANDLE) {
      return false;
    }
  }
  return true;
}

bool
GestureRenderer::ensureSelectionPipelines()
{
  if (m_selectionPipelines[0] != VK_NULL_HANDLE) {
    return true;
  }
  if (m_selectionRenderPass == VK_NULL_HANDLE) {
    return false;
  }
  for (int t = 0; t < kTopologyCount; ++t) {
    m_selectionPipelines[t] = createPipeline(m_selectionRenderPass, static_cast<Topology>(t));
    if (m_selectionPipelines[t] == VK_NULL_HANDLE) {
      return false;
    }
  }
  return true;
}

void
GestureRenderer::uploadVerts(const void* data, size_t byteCount)
{
  VkDevice device = m_backend->logicalDevice();
  if (byteCount > m_vertexCapacity) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, m_vertexBuffer, nullptr);
      vkFreeMemory(device, m_vertexMemory, nullptr);
      m_vertexBuffer = VK_NULL_HANDLE;
      m_vertexMemory = VK_NULL_HANDLE;
    }
    if (!createBuffer(*m_backend,
                      byteCount,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_vertexBuffer,
                      m_vertexMemory)) {
      m_vertexCapacity = 0;
      return;
    }
    m_vertexCapacity = byteCount;
  }
  void* mapped = nullptr;
  vkMapMemory(device, m_vertexMemory, 0, byteCount, 0, &mapped);
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(device, m_vertexMemory);
}

void
GestureRenderer::drawSequences(Framebuffer& target,
                               VkRenderPass renderPass,
                               const std::array<VkPipeline, kTopologyCount>& pipelines,
                               bool clearFirst,
                               SceneView& sceneView,
                               Gesture::Graphics& graphics,
                               const std::vector<int>& sequenceOrder,
                               int picking)
{
  VkDevice device = m_backend->logicalDevice();

  if (clearFirst) {
    // Clear to the "no selection" code (0x7fffffff -> 0xff,0xff,0xff,0x7f).
    target.clear({ 1.0f, 1.0f, 1.0f, 127.0f / 255.0f });
  }

  glm::mat4 viewMatrix(1.0f);
  sceneView.camera.getViewMatrix(viewMatrix);
  glm::mat4 projMatrix(1.0f);
  sceneView.camera.getProjMatrix(projMatrix);
  const glm::mat4 vp = vulkanProjectionCorrection() * projMatrix * viewMatrix;
  const glm::mat4 ortho = vulkanProjectionCorrection() * glm::ortho((float)sceneView.viewport.region.lower.x,
                                                                    (float)sceneView.viewport.region.upper.x,
                                                                    (float)sceneView.viewport.region.lower.y,
                                                                    (float)sceneView.viewport.region.upper.y,
                                                                    1.0f,
                                                                    -1.0f);

  for (int sequence : sequenceOrder) {
    if (graphics.commands[sequence].empty()) {
      continue;
    }

    GuiParams params;
    params.picking = picking;
    params.projection = (sequence == (int)Gesture::Graphics::CommandSequence::k2dScreen) ? ortho : vp;
    void* mapped = nullptr;
    vkMapMemory(device, m_uniformMemory, 0, sizeof(GuiParams), 0, &mapped);
    std::memcpy(mapped, &params, sizeof(GuiParams));
    vkUnmapMemory(device, m_uniformMemory);

    VkCommandBuffer cmd = m_backend->beginSingleTimeCommands();
    target.transitionColorImage(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkFramebuffer vkfb = VK_NULL_HANDLE;
    VkFramebufferCreateInfo fbi = {};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass;
    fbi.attachmentCount = 1;
    VkImageView attachment = target.colorImageView();
    fbi.pAttachments = &attachment;
    fbi.width = target.width();
    fbi.height = target.height();
    fbi.layers = 1;
    if (vkCreateFramebuffer(device, &fbi, nullptr, &vkfb) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateFramebuffer for gesture pass failed";
      m_backend->endSingleTimeCommands(cmd);
      return;
    }

    VkRenderPassBeginInfo rpb = {};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = renderPass;
    rpb.framebuffer = vkfb;
    rpb.renderArea.offset = { 0, 0 };
    rpb.renderArea.extent = { target.width(), target.height() };
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = static_cast<float>(target.width());
    viewport.height = static_cast<float>(target.height());
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = { target.width(), target.height() };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);

    for (Gesture::Graphics::CommandRange cmdr : graphics.commands[sequence]) {
      if (cmdr.end == -1) {
        cmdr.end = static_cast<int>(graphics.verts.size());
      }
      if (cmdr.begin >= cmdr.end) {
        continue;
      }
      const int topo = topologyForCommand(cmdr.command.command);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[topo]);
      vkCmdDraw(cmd, static_cast<uint32_t>(cmdr.end - cmdr.begin), 1, static_cast<uint32_t>(cmdr.begin), 0);
    }

    vkCmdEndRenderPass(cmd);
    m_backend->endSingleTimeCommands(cmd);
    vkDestroyFramebuffer(device, vkfb, nullptr);
  }
}

bool
GestureRenderer::ensureThickLinesResources()
{
  VkDevice device = m_backend->logicalDevice();

  if (m_thickLinesUniformBuffer == VK_NULL_HANDLE &&
      !createBuffer(*m_backend,
                    sizeof(ThickLinesParams),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_thickLinesUniformBuffer,
                    m_thickLinesUniformMemory)) {
    return false;
  }

  if (m_thickLinesDescriptorSetLayout == VK_NULL_HANDLE) {
    // 0: UBO shared by vertex+fragment
    // 1: sampler2D used only by fragment (dummy: strip verts flag "no texture")
    // 2: uniform texel buffer of strip vertex floats, sampled by vertex
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
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(bindings.size());
    li.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &m_thickLinesDescriptorSetLayout) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorSetLayout for gesture thick lines failed";
      return false;
    }

    std::array<VkDescriptorPoolSize, 3> ps = {};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    ps[2].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = static_cast<uint32_t>(ps.size());
    pi.pPoolSizes = ps.data();
    if (vkCreateDescriptorPool(device, &pi, nullptr, &m_thickLinesDescriptorPool) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateDescriptorPool for gesture thick lines failed";
      return false;
    }
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_thickLinesDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_thickLinesDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &ai, &m_thickLinesDescriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for gesture thick lines failed";
      return false;
    }

    // Bindings 0 and 1 never change after creation; binding 2 (the strip-verts
    // texel buffer view) is (re)written by uploadStripVerts() whenever the
    // buffer is (re)allocated.
    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = m_thickLinesUniformBuffer;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(ThickLinesParams);
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = m_dummyView;
    imgInfo.sampler = m_dummySampler;
    std::array<VkWriteDescriptorSet, 2> writes = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_thickLinesDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &bufInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_thickLinesDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  if (m_thickLinesPipelineLayout == VK_NULL_HANDLE) {
    VkPipelineLayoutCreateInfo pli = {};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_thickLinesDescriptorSetLayout;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &m_thickLinesPipelineLayout) != VK_SUCCESS) {
      LOG_ERROR << "vkCreatePipelineLayout for gesture thick lines failed";
      return false;
    }
  }
  return true;
}

VkPipeline
GestureRenderer::createThickLinesPipeline(VkRenderPass renderPass)
{
  VkDevice device = m_backend->logicalDevice();
  const bool blendEnable = (renderPass == m_displayRenderPass);

  VkShaderModule vs = createShaderModule(thickLines_vert_spv, thickLines_vert_spv_word_count);
  VkShaderModule fs = createShaderModule(thickLines_frag_spv, thickLines_frag_spv_word_count);
  if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
    if (vs)
      vkDestroyShaderModule(device, vs, nullptr);
    if (fs)
      vkDestroyShaderModule(device, fs, nullptr);
    return VK_NULL_HANDLE;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  // No vertex input: the vertex shader synthesizes positions from gl_VertexIndex
  // and reads geometry from the uniform texel buffer instead.
  VkPipelineVertexInputStateCreateInfo vi = {};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo ia = {};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp = {};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs = {};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms = {};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba = {};
  cba.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  cba.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo cb = {};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo ds = {};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  ds.dynamicStateCount = static_cast<uint32_t>(dyn.size());
  ds.pDynamicStates = dyn.data();

  VkGraphicsPipelineCreateInfo pi = {};
  pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vi;
  pi.pInputAssemblyState = &ia;
  pi.pViewportState = &vp;
  pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms;
  pi.pColorBlendState = &cb;
  pi.pDynamicState = &ds;
  pi.layout = m_thickLinesPipelineLayout;
  pi.renderPass = renderPass;
  pi.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline);
  vkDestroyShaderModule(device, fs, nullptr);
  vkDestroyShaderModule(device, vs, nullptr);
  if (r != VK_SUCCESS) {
    LOG_ERROR << "vkCreateGraphicsPipelines for gesture thick lines failed with VkResult " << r;
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

bool
GestureRenderer::ensureThickLinesPipelines(VkFormat colorFormat)
{
  VkDevice device = m_backend->logicalDevice();

  const bool displayColorChanged = (m_thickLinesDisplayColorFormat != colorFormat);
  if (m_thickLinesDisplayPipeline == VK_NULL_HANDLE || displayColorChanged) {
    if (m_thickLinesDisplayPipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, m_thickLinesDisplayPipeline, nullptr);
      m_thickLinesDisplayPipeline = VK_NULL_HANDLE;
    }
    m_thickLinesDisplayPipeline = createThickLinesPipeline(m_displayRenderPass);
    if (m_thickLinesDisplayPipeline == VK_NULL_HANDLE) {
      return false;
    }
    m_thickLinesDisplayColorFormat = colorFormat;
  }
  if (m_thickLinesSelectionPipeline == VK_NULL_HANDLE) {
    m_thickLinesSelectionPipeline = createThickLinesPipeline(m_selectionRenderPass);
    if (m_thickLinesSelectionPipeline == VK_NULL_HANDLE) {
      return false;
    }
  }
  return true;
}

void
GestureRenderer::uploadStripVerts(const void* data, size_t byteCount)
{
  VkDevice device = m_backend->logicalDevice();
  const bool reallocate = byteCount > m_stripVertexCapacity;
  if (reallocate) {
    if (m_stripVertexView != VK_NULL_HANDLE) {
      vkDestroyBufferView(device, m_stripVertexView, nullptr);
      m_stripVertexView = VK_NULL_HANDLE;
    }
    if (m_stripVertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, m_stripVertexBuffer, nullptr);
      vkFreeMemory(device, m_stripVertexMemory, nullptr);
      m_stripVertexBuffer = VK_NULL_HANDLE;
      m_stripVertexMemory = VK_NULL_HANDLE;
    }
    if (!createBuffer(*m_backend,
                      byteCount,
                      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_stripVertexBuffer,
                      m_stripVertexMemory)) {
      m_stripVertexCapacity = 0;
      return;
    }
    m_stripVertexCapacity = byteCount;

    // The shader indexes the buffer one float at a time (R32_SFLOAT
    // samplerBuffer), so the buffer view spans the whole allocation as floats.
    VkBufferViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    vi.buffer = m_stripVertexBuffer;
    vi.format = VK_FORMAT_R32_SFLOAT;
    vi.offset = 0;
    vi.range = VK_WHOLE_SIZE;
    if (vkCreateBufferView(device, &vi, nullptr, &m_stripVertexView) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateBufferView for gesture strip verts failed";
      return;
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_thickLinesDescriptorSet;
    write.dstBinding = 2;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    write.descriptorCount = 1;
    write.pTexelBufferView = &m_stripVertexView;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  void* mapped = nullptr;
  vkMapMemory(device, m_stripVertexMemory, 0, byteCount, 0, &mapped);
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(device, m_stripVertexMemory);
}

void
GestureRenderer::drawStrips(Framebuffer& target,
                            VkRenderPass renderPass,
                            VkPipeline pipeline,
                            SceneView& sceneView,
                            Gesture::Graphics& graphics,
                            int picking)
{
  if (graphics.stripRanges.empty() || pipeline == VK_NULL_HANDLE) {
    return;
  }

  VkDevice device = m_backend->logicalDevice();

  glm::mat4 viewMatrix(1.0f);
  sceneView.camera.getViewMatrix(viewMatrix);
  glm::mat4 projMatrix(1.0f);
  sceneView.camera.getProjMatrix(projMatrix);
  const glm::mat4 vpMat = vulkanProjectionCorrection() * projMatrix * viewMatrix;
  const glm::mat4 ortho = vulkanProjectionCorrection() * glm::ortho((float)sceneView.viewport.region.lower.x,
                                                                    (float)sceneView.viewport.region.upper.x,
                                                                    (float)sceneView.viewport.region.lower.y,
                                                                    (float)sceneView.viewport.region.upper.y,
                                                                    1.0f,
                                                                    -1.0f);
  const glm::ivec2 vpSize = sceneView.viewport.region.size();

  for (size_t i = 0; i < graphics.stripRanges.size(); ++i) {
    const glm::ivec2& range = graphics.stripRanges[i];
    // The strip layout adds one leading and one trailing padding vertex for
    // computing miters at the endpoints. See gesture.h::addLineStrip.
    const int totalVerts = range.y - range.x;
    const int N = totalVerts - 2; // real vertices
    const int segments = N - 1;
    if (segments <= 0) {
      continue;
    }

    ThickLinesParams params;
    params.projection = (graphics.stripProjections[i] == Gesture::Graphics::CommandSequence::k2dScreen) ? ortho : vpMat;
    params.resolution = glm::vec2(vpSize);
    params.stripVertexOffset = range.x;
    params.picking = picking;
    params.thickness = graphics.stripThicknesses[i];

    void* mapped = nullptr;
    vkMapMemory(device, m_thickLinesUniformMemory, 0, sizeof(ThickLinesParams), 0, &mapped);
    std::memcpy(mapped, &params, sizeof(ThickLinesParams));
    vkUnmapMemory(device, m_thickLinesUniformMemory);

    VkCommandBuffer cmd = m_backend->beginSingleTimeCommands();
    target.transitionColorImage(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkFramebuffer vkfb = VK_NULL_HANDLE;
    VkFramebufferCreateInfo fbi = {};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass;
    fbi.attachmentCount = 1;
    VkImageView attachment = target.colorImageView();
    fbi.pAttachments = &attachment;
    fbi.width = target.width();
    fbi.height = target.height();
    fbi.layers = 1;
    if (vkCreateFramebuffer(device, &fbi, nullptr, &vkfb) != VK_SUCCESS) {
      LOG_ERROR << "vkCreateFramebuffer for gesture thick-line pass failed";
      m_backend->endSingleTimeCommands(cmd);
      continue;
    }

    VkRenderPassBeginInfo rpb = {};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = renderPass;
    rpb.framebuffer = vkfb;
    rpb.renderArea.offset = { 0, 0 };
    rpb.renderArea.extent = { target.width(), target.height() };
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = static_cast<float>(target.width());
    viewport.height = static_cast<float>(target.height());
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {};
    scissor.extent = { target.width(), target.height() };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_thickLinesPipelineLayout,
                            0,
                            1,
                            &m_thickLinesDescriptorSet,
                            0,
                            nullptr);
    vkCmdDraw(cmd, 6u * static_cast<uint32_t>(segments), 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    m_backend->endSingleTimeCommands(cmd);
    vkDestroyFramebuffer(device, vkfb, nullptr);
  }
}

void
GestureRenderer::drawImpl(SceneView& sceneView, Gesture::Graphics& graphics, const std::vector<int>& sequenceOrder)
{
  if (!ensureBackend()) {
    graphics.clearCommands();
    return;
  }

  auto* target = dynamic_cast<Framebuffer*>(m_target);
  const bool hasVerts = !graphics.verts.empty();
  const bool hasStrips = !graphics.stripRanges.empty() && !graphics.stripVerts.empty();
  if (!target || (!hasVerts && !hasStrips)) {
    // No gizmo geometry this frame. Clear the selection buffer to the
    // "no selection" code so next frame's pick() reports nothing under the
    // cursor; otherwise stale codes make pick() report a false hit, which
    // disables camera manipulation (the tool "grabs" instead of orbiting).
    const glm::ivec2 vpSize = sceneView.viewport.region.size();
    if (ensureSelectionFramebuffer(vpSize.x, vpSize.y) && m_selectionFbo) {
      m_selectionFbo->clear({ 1.0f, 1.0f, 1.0f, 127.0f / 255.0f });
    }
    graphics.clearCommands();
    return;
  }

  const glm::ivec2 vpSize = sceneView.viewport.region.size();
  if (!ensureCommonResources() || !ensureSelectionFramebuffer(vpSize.x, vpSize.y) ||
      !ensureDisplayPipelines(target->colorFormat()) || !ensureSelectionPipelines()) {
    graphics.clearCommands();
    return;
  }

  if (hasVerts) {
    uploadVerts(graphics.verts.data(), graphics.verts.size() * sizeof(Gesture::Graphics::VertsCode));
  }

  bool thickLinesReady = false;
  if (hasStrips && ensureThickLinesResources() && ensureThickLinesPipelines(target->colorFormat())) {
    uploadStripVerts(graphics.stripVerts.data(),
                     graphics.stripVerts.size() * sizeof(Gesture::Graphics::VertsCode));
    thickLinesReady = m_stripVertexBuffer != VK_NULL_HANDLE && m_stripVertexView != VK_NULL_HANDLE;
  }

  // Composite the gizmo overlay onto the target framebuffer. Selection codes
  // are rendered afterwards to an offscreen framebuffer for next-frame picking.
  const bool clearSelection = true;
  if (hasVerts) {
    drawSequences(*target, m_displayRenderPass, m_displayPipelines, false, sceneView, graphics, sequenceOrder, 0);
  }
  if (thickLinesReady) {
    drawStrips(*target, m_displayRenderPass, m_thickLinesDisplayPipeline, sceneView, graphics, 0);
  }
  if (hasVerts) {
    drawSequences(*m_selectionFbo,
                  m_selectionRenderPass,
                  m_selectionPipelines,
                  clearSelection,
                  sceneView,
                  graphics,
                  sequenceOrder,
                  1);
  } else {
    // Still need to clear the selection buffer before drawing strip codes into
    // it, otherwise stale codes from previous frames survive.
    m_selectionFbo->clear({ 1.0f, 1.0f, 1.0f, 127.0f / 255.0f });
  }
  if (thickLinesReady) {
    drawStrips(*m_selectionFbo, m_selectionRenderPass, m_thickLinesSelectionPipeline, sceneView, graphics, 1);
  }

  graphics.clearCommands();
}

void
GestureRenderer::draw(SceneView& sceneView, Gesture::Graphics& graphics)
{
  const std::vector<int> sequenceOrder = {
    (int)Gesture::Graphics::CommandSequence::k3dDepthTested,
    (int)Gesture::Graphics::CommandSequence::k3dStacked,
    (int)Gesture::Graphics::CommandSequence::k2dScreen,
  };
  drawImpl(sceneView, graphics, sequenceOrder);
}

void
GestureRenderer::drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics)
{
  // The underlay pass is drawn before the scene in the GL path (depth-composited).
  // The Vulkan scene pass clears its target, so an overlay drawn here would be
  // wiped; the manipulators use the k3dStacked sequence (handled by draw()), so
  // this is currently a no-op to avoid consuming the shared draw commands early.
  (void)sceneView;
  (void)graphics;
}

bool
GestureRenderer::pick(const Gesture::Input& input, const SceneView::Viewport& viewport, uint32_t& selectionCode)
{
  selectionCode = Gesture::Graphics::k_noSelectionCode;
  if (!ensureBackend() || !m_selectionFbo) {
    return false;
  }
  if (m_selectionFbo->width() != (uint32_t)viewport.region.size().x ||
      m_selectionFbo->height() != (uint32_t)viewport.region.size().y) {
    return false;
  }

  // The selection image is rendered with the same projection as the on-screen
  // gesture overlay and shares the framebuffer's top-left origin, so the cursor
  // position maps directly (no Y flip). (viewport.toRaster() flips Y for the
  // OpenGL bottom-left convention, which is wrong for the Vulkan selection image.)
  glm::ivec2 pixel(static_cast<int>(input.cursorPos.x), static_cast<int>(input.cursorPos.y));
  constexpr int kClickRadius = 7;
  SceneView::Viewport::Region region;
  region.extend(pixel - glm::ivec2(kClickRadius));
  region.extend(pixel + glm::ivec2(kClickRadius));
  SceneView::Viewport::Region viewRegion(viewport.region.lower, viewport.region.upper - glm::ivec2(1));
  region = SceneView::Viewport::Region::intersect(region, viewRegion);
  if (region.empty()) {
    return false;
  }

  const glm::ivec2 regionSize = region.size() + glm::ivec2(1);
  const size_t pixelCount = size_t(regionSize.x) * size_t(regionSize.y);
  const VkDeviceSize byteCount = pixelCount * 4;

  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMem = VK_NULL_HANDLE;
  if (!createBuffer(*m_backend,
                    byteCount,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    staging,
                    stagingMem)) {
    return false;
  }

  VkCommandBuffer cmd = m_backend->beginSingleTimeCommands();
  m_selectionFbo->transitionColorImage(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy copy = {};
  copy.bufferOffset = 0;
  copy.bufferRowLength = 0;
  copy.bufferImageHeight = 0;
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.mipLevel = 0;
  copy.imageSubresource.baseArrayLayer = 0;
  copy.imageSubresource.layerCount = 1;
  copy.imageOffset = { region.lower.x, region.lower.y, 0 };
  copy.imageExtent = { (uint32_t)regionSize.x, (uint32_t)regionSize.y, 1 };
  vkCmdCopyImageToBuffer(cmd, m_selectionFbo->colorImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
  m_backend->endSingleTimeCommands(cmd);

  VkDevice device = m_backend->logicalDevice();
  void* mapped = nullptr;
  vkMapMemory(device, stagingMem, 0, byteCount, 0, &mapped);
  const uint8_t* pixels = static_cast<const uint8_t*>(mapped);
  uint32_t best = Gesture::Graphics::k_noSelectionCode;
  for (size_t i = 0; i < pixelCount; ++i) {
    uint32_t code = selectionRGB8ToCode(pixels + i * 4);
    if (code != Gesture::Graphics::k_noSelectionCode && code < best) {
      best = code;
    }
  }
  vkUnmapMemory(device, stagingMem);
  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, stagingMem, nullptr);

  selectionCode = best;
  return best != Gesture::Graphics::k_noSelectionCode;
}

void
GestureRenderer::destroy()
{
  if (!m_backend) {
    return;
  }
  VkDevice device = m_backend->logicalDevice();
  if (device == VK_NULL_HANDLE) {
    return;
  }
  vkDeviceWaitIdle(device);

  for (auto& p : m_displayPipelines) {
    if (p) {
      vkDestroyPipeline(device, p, nullptr);
      p = VK_NULL_HANDLE;
    }
  }
  for (auto& p : m_selectionPipelines) {
    if (p) {
      vkDestroyPipeline(device, p, nullptr);
      p = VK_NULL_HANDLE;
    }
  }
  if (m_thickLinesDisplayPipeline) {
    vkDestroyPipeline(device, m_thickLinesDisplayPipeline, nullptr);
    m_thickLinesDisplayPipeline = VK_NULL_HANDLE;
  }
  if (m_thickLinesSelectionPipeline) {
    vkDestroyPipeline(device, m_thickLinesSelectionPipeline, nullptr);
    m_thickLinesSelectionPipeline = VK_NULL_HANDLE;
  }
  m_thickLinesDisplayColorFormat = VK_FORMAT_UNDEFINED;
  if (m_displayRenderPass) {
    vkDestroyRenderPass(device, m_displayRenderPass, nullptr);
    m_displayRenderPass = VK_NULL_HANDLE;
  }
  if (m_selectionRenderPass) {
    vkDestroyRenderPass(device, m_selectionRenderPass, nullptr);
    m_selectionRenderPass = VK_NULL_HANDLE;
  }
  if (m_pipelineLayout) {
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
  }
  if (m_thickLinesPipelineLayout) {
    vkDestroyPipelineLayout(device, m_thickLinesPipelineLayout, nullptr);
    m_thickLinesPipelineLayout = VK_NULL_HANDLE;
  }
  if (m_descriptorPool) {
    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSet = VK_NULL_HANDLE;
  }
  if (m_thickLinesDescriptorPool) {
    vkDestroyDescriptorPool(device, m_thickLinesDescriptorPool, nullptr);
    m_thickLinesDescriptorPool = VK_NULL_HANDLE;
    m_thickLinesDescriptorSet = VK_NULL_HANDLE;
  }
  if (m_descriptorSetLayout) {
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    m_descriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_thickLinesDescriptorSetLayout) {
    vkDestroyDescriptorSetLayout(device, m_thickLinesDescriptorSetLayout, nullptr);
    m_thickLinesDescriptorSetLayout = VK_NULL_HANDLE;
  }
  if (m_dummySampler) {
    vkDestroySampler(device, m_dummySampler, nullptr);
    m_dummySampler = VK_NULL_HANDLE;
  }
  if (m_dummyView) {
    vkDestroyImageView(device, m_dummyView, nullptr);
    m_dummyView = VK_NULL_HANDLE;
  }
  if (m_dummyImage) {
    vkDestroyImage(device, m_dummyImage, nullptr);
    m_dummyImage = VK_NULL_HANDLE;
  }
  if (m_dummyMemory) {
    vkFreeMemory(device, m_dummyMemory, nullptr);
    m_dummyMemory = VK_NULL_HANDLE;
  }
  if (m_uniformBuffer) {
    vkDestroyBuffer(device, m_uniformBuffer, nullptr);
    m_uniformBuffer = VK_NULL_HANDLE;
  }
  if (m_uniformMemory) {
    vkFreeMemory(device, m_uniformMemory, nullptr);
    m_uniformMemory = VK_NULL_HANDLE;
  }
  if (m_thickLinesUniformBuffer) {
    vkDestroyBuffer(device, m_thickLinesUniformBuffer, nullptr);
    m_thickLinesUniformBuffer = VK_NULL_HANDLE;
  }
  if (m_thickLinesUniformMemory) {
    vkFreeMemory(device, m_thickLinesUniformMemory, nullptr);
    m_thickLinesUniformMemory = VK_NULL_HANDLE;
  }
  if (m_vertexBuffer) {
    vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    m_vertexBuffer = VK_NULL_HANDLE;
  }
  if (m_vertexMemory) {
    vkFreeMemory(device, m_vertexMemory, nullptr);
    m_vertexMemory = VK_NULL_HANDLE;
  }
  if (m_stripVertexView) {
    vkDestroyBufferView(device, m_stripVertexView, nullptr);
    m_stripVertexView = VK_NULL_HANDLE;
  }
  if (m_stripVertexBuffer) {
    vkDestroyBuffer(device, m_stripVertexBuffer, nullptr);
    m_stripVertexBuffer = VK_NULL_HANDLE;
  }
  if (m_stripVertexMemory) {
    vkFreeMemory(device, m_stripVertexMemory, nullptr);
    m_stripVertexMemory = VK_NULL_HANDLE;
  }
  m_stripVertexCapacity = 0;
  m_selectionFbo.reset();
}

} // namespace gfxvulkan
