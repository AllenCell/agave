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

} // namespace gfxvulkan
