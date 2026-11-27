#include "GestureRenderer.h"

#include "CCamera.h"
#include "Font.h"
#include "Framebuffer.h"
#include "Logging.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"
#include "gfxapi/Backend.h"
#include "gfxapi/Framebuffer.h"
#include "renderlib.h"

#include "gfxVulkan/shadersrc/gui_frag_spv.hpp"
#include "gfxVulkan/shadersrc/gui_vert_spv.hpp"
#include "gfxVulkan/shadersrc/thickLines_frag_spv.hpp"
#include "gfxVulkan/shadersrc/thickLines_vert_spv.hpp"

#include <array>
#include <cstring>
#include <utility>
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

std::optional<resources::UniqueRenderPass>
createColorRenderPass(Backend& backend, VkFormat colorFormat)
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

  return backend.device().createRenderPass(info);
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

bool
GestureRenderer::ensureCommonResources()
{
  VkDevice device = m_backend->logicalDevice();

  if (!m_uniformBuffer) {
    auto buffer =
      m_backend->device().createBuffer(sizeof(GuiParams),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer) {
      return false;
    }
    m_uniformBuffer = std::move(*buffer);
  }

  // 1x1 white placeholder for the gui Texture binding. Gizmo verts flag
  // "no texture" via uv < -64, so this is never actually sampled for them.
  if (!m_dummyTexture) {
    auto image = m_backend->device().createImage(1,
                                                 1,
                                                 1,
                                                 1,
                                                 VK_FORMAT_R8G8B8A8_UNORM,
                                                 VK_IMAGE_TYPE_2D,
                                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (!image) {
      return false;
    }
    auto view = m_backend->device().createImageView(
      image->get(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (!view) {
      return false;
    }
    VkSamplerCreateInfo s = {};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_NEAREST;
    s.minFilter = VK_FILTER_NEAREST;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto sampler = m_backend->device().createSampler(s);
    if (!sampler) {
      return false;
    }
    transitionImageLayout(*m_backend,
                          image->get(),
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          1);
    m_dummyTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));
  }

  if (!m_descriptorSetLayout) {
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
    auto descriptorSetLayoutResource = m_backend->device().createDescriptorSetLayout(li);
    if (!descriptorSetLayoutResource) {
      return false;
    }
    m_descriptorSetLayout = std::move(*descriptorSetLayoutResource);

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
    auto descriptorPool = m_backend->device().createDescriptorPool(pi);
    if (!descriptorPool) {
      return false;
    }
    m_descriptorPool = std::move(*descriptorPool);
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPool.get();
    ai.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_descriptorSetLayout.get();
    ai.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &ai, &m_descriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for gesture failed";
      return false;
    }

    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = m_uniformBuffer.get();
    bufInfo.offset = 0;
    bufInfo.range = sizeof(GuiParams);
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = m_dummyTexture.view();
    imgInfo.sampler = m_dummyTexture.sampler();
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

  if (!m_pipelineLayout) {
    VkPipelineLayoutCreateInfo pli = {};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_descriptorSetLayout.get();
    pli.pSetLayouts = &descriptorSetLayout;
    auto pipelineLayout = m_backend->device().createPipelineLayout(pli);
    if (!pipelineLayout) {
      return false;
    }
    m_pipelineLayout = std::move(*pipelineLayout);
  }
  return true;
}

bool
GestureRenderer::ensureFontResources(const AgaveFont& font)
{
  // Nothing to do until the font atlas has been baked, or if we've already
  // uploaded it. Font atlas contents are baked once at load time and never
  // change, so a single upload is sufficient for the lifetime of the renderer.
  if (m_fontTexture) {
    return true;
  }
  const uint32_t w = font.getTextureWidth();
  const uint32_t h = font.getTextureHeight();
  const unsigned char* alpha = font.getTextureData();
  if (w == 0 || h == 0 || alpha == nullptr) {
    return false;
  }

  VkDevice device = m_backend->logicalDevice();

  // Expand the single-channel alpha atlas to RGBA8 with white RGB, matching
  // the OpenGL FontGL path so the gui shader's `result *= texture(...)`
  // multiplication produces vertex-colored glyphs with correct coverage.
  const VkDeviceSize byteCount = static_cast<VkDeviceSize>(w) * h * 4;
  auto staging =
    m_backend->device().createBuffer(byteCount,
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!staging) {
    return false;
  }
  void* mapped = nullptr;
  if (vkMapMemory(device, staging->memory(), 0, byteCount, 0, &mapped) != VK_SUCCESS) {
    return false;
  }
  auto* dst = static_cast<uint8_t*>(mapped);
  const size_t pixelCount = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < pixelCount; ++i) {
    dst[i * 4 + 0] = 255;
    dst[i * 4 + 1] = 255;
    dst[i * 4 + 2] = 255;
    dst[i * 4 + 3] = alpha[i];
  }
  vkUnmapMemory(device, staging->memory());

  auto image = m_backend->device().createImage(w,
                                               h,
                                               1,
                                               1,
                                               VK_FORMAT_R8G8B8A8_UNORM,
                                               VK_IMAGE_TYPE_2D,
                                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  if (!image) {
    return false;
  }
  auto view = m_backend->device().createImageView(
    image->get(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  if (!view) {
    return false;
  }

  transitionImageLayout(*m_backend,
                        image->get(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1);
  copyBufferToImage(*m_backend, staging->get(), image->get(), w, h, 1, 1);
  transitionImageLayout(*m_backend,
                        image->get(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        1);
  // Linear filtering matches FontGL (which sets GL_LINEAR on the atlas).
  VkSamplerCreateInfo s = {};
  s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  s.magFilter = VK_FILTER_LINEAR;
  s.minFilter = VK_FILTER_LINEAR;
  s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  auto sampler = m_backend->device().createSampler(s);
  if (!sampler) {
    return false;
  }

  m_fontTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));

  // Point the gui shader's Texture binding (set 0, binding 1) at the font
  // atlas instead of the 1x1 dummy image. Safe to do while no command buffer
  // is in flight: draws are submitted via endSingleTimeCommands which waits
  // on the queue before returning.
  VkDescriptorImageInfo imgInfo = {};
  imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imgInfo.imageView = m_fontTexture.view();
  imgInfo.sampler = m_fontTexture.sampler();
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = m_descriptorSet;
  write.dstBinding = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imgInfo;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

  m_fontWidth = w;
  m_fontHeight = h;
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
  if (!m_selectionRenderPass) {
    auto renderPass = createColorRenderPass(*m_backend, m_selectionFbo->colorFormat());
    if (!renderPass) {
      return false;
    }
    m_selectionRenderPass = std::move(*renderPass);
  }
  return true;
}

std::optional<resources::UniquePipeline>
GestureRenderer::createPipeline(VkRenderPass renderPass, Topology topology)
{
  const bool blendEnable = (renderPass == m_displayRenderPass.get());

  auto vs = m_backend->device().createShaderModule(gui_vert_spv, gui_vert_spv_word_count);
  auto fs = m_backend->device().createShaderModule(gui_frag_spv, gui_frag_spv_word_count);
  if (!vs || !fs) {
    return std::nullopt;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs->get();
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs->get();
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
  pi.layout = m_pipelineLayout.get();
  pi.renderPass = renderPass;
  pi.subpass = 0;

  return m_backend->device().createPipeline(pi);
}

bool
GestureRenderer::ensureDisplayPipelines(VkFormat colorFormat)
{
  if (m_displayRenderPass && m_displayColorFormat == colorFormat && m_displayPipelines[0]) {
    return true;
  }
  for (auto& p : m_displayPipelines) {
    p.reset();
  }
  m_displayRenderPass.reset();
  auto renderPass = createColorRenderPass(*m_backend, colorFormat);
  if (!renderPass) {
    return false;
  }
  m_displayRenderPass = std::move(*renderPass);
  m_displayColorFormat = colorFormat;
  for (int t = 0; t < kTopologyCount; ++t) {
    auto pipeline = createPipeline(m_displayRenderPass.get(), static_cast<Topology>(t));
    if (!pipeline) {
      return false;
    }
    m_displayPipelines[t] = std::move(*pipeline);
  }
  return true;
}

bool
GestureRenderer::ensureSelectionPipelines()
{
  if (m_selectionPipelines[0]) {
    return true;
  }
  if (!m_selectionRenderPass) {
    return false;
  }
  for (int t = 0; t < kTopologyCount; ++t) {
    auto pipeline = createPipeline(m_selectionRenderPass.get(), static_cast<Topology>(t));
    if (!pipeline) {
      return false;
    }
    m_selectionPipelines[t] = std::move(*pipeline);
  }
  return true;
}

void
GestureRenderer::uploadVerts(const void* data, size_t byteCount)
{
  VkDevice device = m_backend->logicalDevice();
  if (byteCount > m_vertexCapacity) {
    m_vertexBuffer.reset();
    auto buffer =
      m_backend->device().createBuffer(byteCount,
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer) {
      m_vertexCapacity = 0;
      return;
    }
    m_vertexBuffer = std::move(*buffer);
    m_vertexCapacity = byteCount;
  }
  void* mapped = nullptr;
  vkMapMemory(device, m_vertexBuffer.memory(), 0, byteCount, 0, &mapped);
  std::memcpy(mapped, data, byteCount);
  vkUnmapMemory(device, m_vertexBuffer.memory());
}

void
GestureRenderer::drawSequences(Framebuffer& target,
                               VkRenderPass renderPass,
                               const std::array<resources::UniquePipeline, kTopologyCount>& pipelines,
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
    vkMapMemory(device, m_uniformBuffer.memory(), 0, sizeof(GuiParams), 0, &mapped);
    std::memcpy(mapped, &params, sizeof(GuiParams));
    vkUnmapMemory(device, m_uniformBuffer.memory());

    VkCommandBuffer cmd = m_backend->beginSingleTimeCommands();
    target.transitionColorImage(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkFramebufferCreateInfo fbi = {};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass;
    fbi.attachmentCount = 1;
    VkImageView attachment = target.colorImageView();
    fbi.pAttachments = &attachment;
    fbi.width = target.width();
    fbi.height = target.height();
    fbi.layers = 1;
    auto vkfb = m_backend->device().createFramebuffer(fbi);
    if (!vkfb) {
      m_backend->endSingleTimeCommands(cmd);
      return;
    }

    VkRenderPassBeginInfo rpb = {};
    rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass = renderPass;
    rpb.framebuffer = vkfb->get();
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
    vkCmdBindDescriptorSets(
      cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout.get(), 0, 1, &m_descriptorSet, 0, nullptr);
    VkDeviceSize offset = 0;
    VkBuffer vertexBuffer = m_vertexBuffer.get();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

    for (Gesture::Graphics::CommandRange cmdr : graphics.commands[sequence]) {
      if (cmdr.end == -1) {
        cmdr.end = static_cast<int>(graphics.verts.size());
      }
      if (cmdr.begin >= cmdr.end) {
        continue;
      }
      const int topo = topologyForCommand(cmdr.command.command);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[topo].get());
      vkCmdDraw(cmd, static_cast<uint32_t>(cmdr.end - cmdr.begin), 1, static_cast<uint32_t>(cmdr.begin), 0);
    }

    vkCmdEndRenderPass(cmd);
    m_backend->endSingleTimeCommands(cmd);
  }
}

bool
GestureRenderer::ensureThickLinesResources()
{
  VkDevice device = m_backend->logicalDevice();

  if (!m_thickLinesUniformBuffer) {
    auto buffer =
      m_backend->device().createBuffer(sizeof(ThickLinesParams),
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer) {
      return false;
    }
    m_thickLinesUniformBuffer = std::move(*buffer);
  }

  if (!m_thickLinesDescriptorSetLayout) {
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
    auto descriptorSetLayoutResource = m_backend->device().createDescriptorSetLayout(li);
    if (!descriptorSetLayoutResource) {
      return false;
    }
    m_thickLinesDescriptorSetLayout = std::move(*descriptorSetLayoutResource);

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
    auto descriptorPool = m_backend->device().createDescriptorPool(pi);
    if (!descriptorPool) {
      return false;
    }
    m_thickLinesDescriptorPool = std::move(*descriptorPool);
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_thickLinesDescriptorPool.get();
    ai.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_thickLinesDescriptorSetLayout.get();
    ai.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &ai, &m_thickLinesDescriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for gesture thick lines failed";
      return false;
    }

    // Bindings 0 and 1 never change after creation; binding 2 (the strip-verts
    // texel buffer view) is (re)written by uploadStripVerts() whenever the
    // buffer is (re)allocated.
    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = m_thickLinesUniformBuffer.get();
    bufInfo.offset = 0;
    bufInfo.range = sizeof(ThickLinesParams);
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = m_dummyTexture.view();
    imgInfo.sampler = m_dummyTexture.sampler();
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

  if (!m_thickLinesPipelineLayout) {
    VkPipelineLayoutCreateInfo pli = {};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_thickLinesDescriptorSetLayout.get();
    pli.pSetLayouts = &descriptorSetLayout;
    auto pipelineLayout = m_backend->device().createPipelineLayout(pli);
    if (!pipelineLayout) {
      return false;
    }
    m_thickLinesPipelineLayout = std::move(*pipelineLayout);
  }
  return true;
}

} // namespace gfxvulkan
