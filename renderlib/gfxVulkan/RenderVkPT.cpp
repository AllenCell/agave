#include "RenderVkPT.h"

#include "AppScene.h"
#include "CCamera.h"
#include "Framebuffer.h"
#include "GradientData.h"
#include "Histogram.h"
#include "ImageXYZC.h"
#include "Light.h"
#include "Logging.h"
#include "MathUtil.h"
#include "RenderSettings.h"
#include "VolumeTextureVk.h"
#include "Timing.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"
#include "gfxVulkan/shadersrc/pathTraceVolume_frag_spv.hpp"
#include "gfxVulkan/shadersrc/pathTraceVolume_vert_spv.hpp"
#include "gfxVulkan/shadersrc/ptAccum_frag_spv.hpp"
#include "gfxVulkan/shadersrc/ptAccum_vert_spv.hpp"
#include "gfxVulkan/shadersrc/toneMap_frag_spv.hpp"
#include "gfxVulkan/shadersrc/toneMap_vert_spv.hpp"
#include "glm.h"

#include <algorithm>
#include <array>
#include <chrono>
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

// std140 layout mirror of the PathTraceVolumeParams uniform block in
// pathTraceVolume.frag. Every vec3 is padded to a 16-byte boundary, and scalar
// and vec2/vec3 arrays use a 16-byte stride, exactly as std140 requires. The
// static_asserts below pin the offsets so a layout mistake is a compile error
// rather than a silent GPU garbage render.
constexpr uint32_t kMaxNoTfNodes = 16u;

struct alignas(16) GpuPtCamera
{
  glm::vec3 m_from;
  float _p0;
  glm::vec3 m_U;
  float _p1;
  glm::vec3 m_V;
  float _p2;
  glm::vec3 m_N;
  float _p3;
  glm::vec4 m_screen;
  glm::vec2 m_invScreen;
  float m_focalDistance;
  float m_apertureSize;
  float m_isPerspective;
  float _p4[3];
};
static_assert(sizeof(GpuPtCamera) == 112, "GpuPtCamera std140 size mismatch");
static_assert(offsetof(GpuPtCamera, m_screen) == 64, "GpuPtCamera.m_screen offset");
static_assert(offsetof(GpuPtCamera, m_isPerspective) == 96, "GpuPtCamera.m_isPerspective offset");

struct alignas(16) GpuPtLight
{
  float m_theta, m_phi, m_width, m_halfWidth;
  float m_height, m_halfHeight, m_distance, m_skyRadius;
  glm::vec3 m_P;
  float _pP;
  glm::vec3 m_target;
  float _pT;
  glm::vec3 m_N;
  float _pN;
  glm::vec3 m_U;
  float _pU;
  glm::vec3 m_V;
  float m_area;
  float m_areaPdf;
  float _pad0[3];
  glm::vec3 m_color;
  float _pC;
  glm::vec3 m_colorTop;
  float _pCT;
  glm::vec3 m_colorMiddle;
  float _pCM;
  glm::vec3 m_colorBottom;
  int m_T;
};
static_assert(sizeof(GpuPtLight) == 192, "GpuPtLight std140 size mismatch");
static_assert(offsetof(GpuPtLight, m_P) == 32, "GpuPtLight.m_P offset");
static_assert(offsetof(GpuPtLight, m_area) == 108, "GpuPtLight.m_area offset");
static_assert(offsetof(GpuPtLight, m_color) == 128, "GpuPtLight.m_color offset");
static_assert(offsetof(GpuPtLight, m_T) == 188, "GpuPtLight.m_T offset");

struct alignas(16) PtVolumeUniforms
{
  GpuPtCamera gCamera;
  GpuPtLight gLights[2];
  glm::vec3 gClippedAaBbMin;
  float _padMin;
  glm::vec3 gClippedAaBbMax;
  float gDensityScale;
  float gStepSize;
  float gStepSizeShadow;
  float _padStep[2]; // pad so gPosToUVW (vec3) lands on the std140 16-byte boundary (offset 544)
  glm::vec3 gPosToUVW;
  int g_nChannels;
  int gShadingType;
  float _padShade[3];
  glm::vec3 gGradientDeltaX;
  float _pgx;
  glm::vec3 gGradientDeltaY;
  float _pgy;
  glm::vec3 gGradientDeltaZ;
  float gInvGradientDelta;
  float gGradientFactor;
  float uShowLights;
  float _padShow[2];
  glm::vec4 g_intensityMax;
  glm::vec4 g_intensityMin;
  glm::vec4 g_lutMax;
  glm::vec4 g_lutMin;
  glm::vec4 g_labels;
  glm::vec4 g_opacity[4];  // float[4], std140 stride 16; value in .x
  glm::vec4 g_emissive[4]; // vec3[4], std140 stride 16; value in .xyz
  glm::vec4 g_diffuse[4];
  glm::vec4 g_specular[4];
  glm::vec4 g_roughness[4];          // float[4]; value in .x
  glm::vec4 g_tf[4 * kMaxNoTfNodes]; // vec2[64], std140 stride 16; value in .xy
  glm::uvec4 g_tf_nNodes;
  float uFrameCounter;
  float uSampleCounter;
  glm::vec2 uResolution;
  glm::vec4 g_clipPlane;
};
static_assert(offsetof(PtVolumeUniforms, gLights) == 112, "PtVolumeUniforms.gLights offset");
static_assert(offsetof(PtVolumeUniforms, gClippedAaBbMin) == 496, "PtVolumeUniforms.gClippedAaBbMin offset");
static_assert(offsetof(PtVolumeUniforms, gPosToUVW) == 544, "PtVolumeUniforms.gPosToUVW offset");
static_assert(offsetof(PtVolumeUniforms, gGradientDeltaX) == 576, "PtVolumeUniforms.gGradientDeltaX offset");
static_assert(offsetof(PtVolumeUniforms, g_intensityMax) == 640, "PtVolumeUniforms.g_intensityMax offset");
static_assert(offsetof(PtVolumeUniforms, g_opacity) == 720, "PtVolumeUniforms.g_opacity offset");
static_assert(offsetof(PtVolumeUniforms, g_emissive) == 784, "PtVolumeUniforms.g_emissive offset");
static_assert(offsetof(PtVolumeUniforms, g_tf) == 1040, "PtVolumeUniforms.g_tf offset");
static_assert(offsetof(PtVolumeUniforms, g_tf_nNodes) == 2064, "PtVolumeUniforms.g_tf_nNodes offset");
static_assert(offsetof(PtVolumeUniforms, uFrameCounter) == 2080, "PtVolumeUniforms.uFrameCounter offset");
static_assert(offsetof(PtVolumeUniforms, g_clipPlane) == 2096, "PtVolumeUniforms.g_clipPlane offset");
static_assert(sizeof(PtVolumeUniforms) == 2112, "PtVolumeUniforms std140 size mismatch");

const std::array<FullscreenVertex, 4> kFullscreenVertices = {
  FullscreenVertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
  FullscreenVertex{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
  FullscreenVertex{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
  FullscreenVertex{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
};

const std::array<uint16_t, 6> kFullscreenIndices = { 0, 1, 2, 2, 3, 0 };

template<typename T>
bool
uploadHostBuffer(Backend& backend, VkBufferUsageFlags usage, const T* data, size_t count, resources::Buffer& buffer)
{
  const VkDeviceSize byteCount = static_cast<VkDeviceSize>(sizeof(T) * count);
  auto resource = backend.device().createBuffer(
    byteCount, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!resource) {
    return false;
  }

  void* mapped = nullptr;
  if (vkMapMemory(backend.logicalDevice(), resource->memory(), 0, byteCount, 0, &mapped) != VK_SUCCESS) {
    return false;
  }
  std::memcpy(mapped, data, static_cast<size_t>(byteCount));
  vkUnmapMemory(backend.logicalDevice(), resource->memory());
  buffer = std::move(*resource);
  return true;
}

std::optional<resources::Buffer>
createUniformBuffer(Backend& backend, VkDeviceSize size)
{
  return backend.device().createBuffer(size,
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

std::optional<resources::UniqueRenderPass>
createColorRenderPass(Backend& backend, VkFormat colorFormat, VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR)
{
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = colorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = loadOp;
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

  return backend.device().createRenderPass(renderPassInfo);
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

  if (!m_quadVertexBuffer && !uploadHostBuffer(m_backend,
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               kFullscreenVertices.data(),
                                               kFullscreenVertices.size(),
                                               m_quadVertexBuffer)) {
    return false;
  }

  if (!m_quadIndexBuffer && !uploadHostBuffer(m_backend,
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                              kFullscreenIndices.data(),
                                              kFullscreenIndices.size(),
                                              m_quadIndexBuffer)) {
    return false;
  }
  m_quadIndexCount = static_cast<uint32_t>(kFullscreenIndices.size());

  if (!m_accumUniformBuffer) {
    auto buffer = createUniformBuffer(m_backend, sizeof(PtAccumUniforms));
    if (!buffer) {
      return false;
    }
    m_accumUniformBuffer = std::move(*buffer);
  }
  if (!m_toneMapUniformBuffer) {
    auto buffer = createUniformBuffer(m_backend, sizeof(ToneMapUniforms));
    if (!buffer) {
      return false;
    }
    m_toneMapUniformBuffer = std::move(*buffer);
  }

  if (!m_framebufferSampler) {
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // NEAREST matches the OpenGL RenderGLPT path (GLFramebufferObject uses
    // GL_NEAREST for its color textures). The path-trace accumulator and the
    // tone-map input are sampled at fragment centers on a full-screen quad, so
    // LINEAR filtering here would let sub-pixel rasterization offsets blend
    // adjacent texels -- subtly bleeding partial-coverage neighbors into fully
    // covered pixels and altering the visible edge softness relative to
    // OpenGL.
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    auto sampler = m_backend.device().createSampler(samplerInfo);
    if (!sampler) {
      return false;
    }
    m_framebufferSampler = std::move(*sampler);
  }

  if (!m_accumDescriptorSetLayout) {
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
    auto descriptorSetLayoutResource = m_backend.device().createDescriptorSetLayout(layoutInfo);
    if (!descriptorSetLayoutResource) {
      return false;
    }
    m_accumDescriptorSetLayout = std::move(*descriptorSetLayoutResource);

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
    auto descriptorPool = m_backend.device().createDescriptorPool(poolInfo);
    if (!descriptorPool) {
      return false;
    }
    m_accumDescriptorPool = std::move(*descriptorPool);

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_accumDescriptorPool.get();
    allocateInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_accumDescriptorSetLayout.get();
    allocateInfo.pSetLayouts = &descriptorSetLayout;
    VkResult result = vkAllocateDescriptorSets(device, &allocateInfo, &m_accumDescriptorSet);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for pathtrace accumulation failed with VkResult " << result;
      return false;
    }
  }

  if (!m_toneMapDescriptorSetLayout) {
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
    auto descriptorSetLayoutResource = m_backend.device().createDescriptorSetLayout(layoutInfo);
    if (!descriptorSetLayoutResource) {
      return false;
    }
    m_toneMapDescriptorSetLayout = std::move(*descriptorSetLayoutResource);

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
    auto descriptorPool = m_backend.device().createDescriptorPool(poolInfo);
    if (!descriptorPool) {
      return false;
    }
    m_toneMapDescriptorPool = std::move(*descriptorPool);

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_toneMapDescriptorPool.get();
    allocateInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_toneMapDescriptorSetLayout.get();
    allocateInfo.pSetLayouts = &descriptorSetLayout;
    VkResult result = vkAllocateDescriptorSets(device, &allocateInfo, &m_toneMapDescriptorSet);
    if (result != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for pathtrace tone map failed with VkResult " << result;
      return false;
    }
  }

  if (m_accumPipeline && m_toneMapPipeline && m_toneMapPipelineColorFormat == toneMapFormat) {
    return true;
  }

  destroyPipelines();
  m_toneMapPipelineColorFormat = toneMapFormat;

  auto renderPass = createColorRenderPass(m_backend, VK_FORMAT_R32G32B32A32_SFLOAT);
  if (!renderPass) {
    return false;
  }
  m_accumRenderPass = std::move(*renderPass);

  // Tone map is the final blit into the caller's framebuffer. Preserve
  // existing contents (e.g. the gesture underlay's back-facing bounding-box
  // edges drawn beforehand) so the tone-mapped volume can alpha-blend
  // against them.
  renderPass = createColorRenderPass(m_backend, toneMapFormat, VK_ATTACHMENT_LOAD_OP_LOAD);
  if (!renderPass) {
    return false;
  }
  m_toneMapRenderPass = std::move(*renderPass);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  VkDescriptorSetLayout descriptorSetLayout = m_accumDescriptorSetLayout.get();
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  auto pipelineLayout = m_backend.device().createPipelineLayout(pipelineLayoutInfo);
  if (!pipelineLayout) {
    return false;
  }
  m_accumPipelineLayout = std::move(*pipelineLayout);

  descriptorSetLayout = m_toneMapDescriptorSetLayout.get();
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayout = m_backend.device().createPipelineLayout(pipelineLayoutInfo);
  if (!pipelineLayout) {
    return false;
  }
  m_toneMapPipelineLayout = std::move(*pipelineLayout);

  auto createFullscreenPipeline = [&](VkRenderPass renderPass,
                                      VkPipelineLayout pipelineLayout,
                                      const uint32_t* vertexWords,
                                      size_t vertexWordCount,
                                      const uint32_t* fragmentWords,
                                      size_t fragmentWordCount,
                                      bool includeUv,
                                      bool enableBlend) -> std::optional<resources::UniquePipeline> {
    auto vertexShader = m_backend.device().createShaderModule(vertexWords, vertexWordCount);
    auto fragmentShader = m_backend.device().createShaderModule(fragmentWords, fragmentWordCount);
    if (!vertexShader || !fragmentShader) {
      return std::nullopt;
    }

    VkPipelineShaderStageCreateInfo vertexStage = {};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader->get();
    vertexStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragmentStage = {};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader->get();
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
    colorBlendAttachment.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;
    if (enableBlend) {
      // Straight-alpha blending: dst = src.rgb*src.a + dst.rgb*(1-src.a).
      // Mirrors the OpenGL path (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) so the
      // tone-mapped path-traced image composites over the pre-drawn underlay.
      colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
      colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
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

    return m_backend.device().createPipeline(pipelineInfo);
  };

  auto pipeline = createFullscreenPipeline(m_accumRenderPass.get(),
                                           m_accumPipelineLayout.get(),
                                           ptAccum_vert_spv,
                                           ptAccum_vert_spv_word_count,
                                           ptAccum_frag_spv,
                                           ptAccum_frag_spv_word_count,
                                           false,
                                           false);
  if (!pipeline) {
    return false;
  }
  m_accumPipeline = std::move(*pipeline);

  pipeline = createFullscreenPipeline(m_toneMapRenderPass.get(),
                                      m_toneMapPipelineLayout.get(),
                                      toneMap_vert_spv,
                                      toneMap_vert_spv_word_count,
                                      toneMap_frag_spv,
                                      toneMap_frag_spv_word_count,
                                      true,
                                      true);
  if (!pipeline) {
    return false;
  }
  m_toneMapPipeline = std::move(*pipeline);
  return true;
}

bool
RenderVkPT::updateAccumUniformBuffer()
{
  PtAccumUniforms uniforms;
  uniforms.numIterations = std::max(1, m_renderSettings ? m_renderSettings->GetNoIterations() : 1);

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), m_accumUniformBuffer.memory(), 0, sizeof(PtAccumUniforms), 0, &mapped);
  std::memcpy(mapped, &uniforms, sizeof(PtAccumUniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_accumUniformBuffer.memory());
  return true;
}

bool
RenderVkPT::updateToneMapUniformBuffer(const CCamera& camera)
{
  ToneMapUniforms uniforms;
  uniforms.inverseExposure = 1.0f / std::max(camera.m_Film.m_Exposure, 0.0001f);

  void* mapped = nullptr;
  vkMapMemory(m_backend.logicalDevice(), m_toneMapUniformBuffer.memory(), 0, sizeof(ToneMapUniforms), 0, &mapped);
  std::memcpy(mapped, &uniforms, sizeof(ToneMapUniforms));
  vkUnmapMemory(m_backend.logicalDevice(), m_toneMapUniformBuffer.memory());
  return true;
}

bool
RenderVkPT::updateAccumDescriptorSet()
{
  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = m_accumUniformBuffer.get();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(PtAccumUniforms);

  VkDescriptorImageInfo renderInfo = {};
  renderInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  renderInfo.imageView = m_sampleFramebuffer->colorImageView();
  renderInfo.sampler = m_framebufferSampler.get();

  VkDescriptorImageInfo accumInfo = {};
  accumInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  accumInfo.imageView = m_accumFramebuffer->colorImageView();
  accumInfo.sampler = m_framebufferSampler.get();

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
  bufferInfo.buffer = m_toneMapUniformBuffer.get();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(ToneMapUniforms);

  VkDescriptorImageInfo accumInfo = {};
  accumInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  accumInfo.imageView = m_accumFramebuffer->colorImageView();
  accumInfo.sampler = m_framebufferSampler.get();

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

  const auto frameStart = std::chrono::high_resolution_clock::now();

  const uint32_t renderWidth = std::max<uint32_t>(1, m_w);
  const uint32_t renderHeight = std::max<uint32_t>(1, m_h);
  if (!ensureFramebuffers(renderWidth, renderHeight) || !ensureFullscreenResources(framebuffer.colorFormat()) ||
      !ensurePtVolumeResources()) {
    framebuffer.clear({});
    return;
  }

  // Upload the volume if needed, reset the accumulation counter when the view
  // changed, and clear the dirty flags. (Mirrors what the base per-sample render
  // would have done; we drive the path-trace pass directly instead.)
  //
  // Recompute the derived light geometry (position, basis frame, area, sky
  // radius, area pdf, ...) from the current scene bounding box before
  // prepareToRender() clears LightsDirty. Mirrors RenderGLPT::doRender.
  if (m_renderSettings->m_DirtyFlags.HasFlag(LightsDirty)) {
    for (int i = 0; i < m_scene->m_lighting.m_NoLights; ++i) {
      m_scene->m_lighting.m_Lights[i]->Update(m_scene->m_boundingBox);
    }
  }

  if (!prepareToRender()) {
    framebuffer.clear({});
    return;
  }

  // Each iteration computes one Monte Carlo sample and blends it against the
  // previous accumulation using the shader's in-shader cumulative moving
  // average. m_accumFramebuffer holds the running estimate; we ping-pong with
  // m_accumScratchFramebuffer so the freshly accumulated result becomes the new
  // m_accumFramebuffer (no explicit copy needed).
  const int exposureIterations = std::max(1, camera.m_Film.m_ExposureIterations);
  const auto sampleStart = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < exposureIterations; ++i) {
    const int sampleCounter = m_renderSettings->GetNoIterations();

    transitionToShaderRead(*m_accumFramebuffer);
    if (!updatePtVolumeUniforms(camera, sampleCounter) ||
        !updatePtVolumeDescriptorSet(m_accumFramebuffer->colorImageView())) {
      framebuffer.clear({});
      return;
    }
    renderPtVolume(*m_accumScratchFramebuffer);
    std::swap(m_accumFramebuffer, m_accumScratchFramebuffer);

    m_renderSettings->SetNoIterations(sampleCounter + 1);
  }
  const auto sampleEnd = std::chrono::high_resolution_clock::now();

  transitionToShaderRead(*m_accumFramebuffer);
  updateToneMapUniformBuffer(camera);
  updateToneMapDescriptorSet();
  runToneMapPass(framebuffer);

  const auto frameEnd = std::chrono::high_resolution_clock::now();
  const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
  const double sampleMs = std::chrono::duration<double, std::milli>(sampleEnd - sampleStart).count();
  const double toneMapMs = std::chrono::duration<double, std::milli>(frameEnd - sampleEnd).count();
  m_status->SetStatisticChanged("Performance", "Render Image", std::to_string(frameMs), "ms.");
  m_status->SetStatisticChanged("Performance", "Path Trace Samples", std::to_string(sampleMs), "ms.");
  m_status->SetStatisticChanged("Performance", "Tone Map", std::to_string(toneMapMs), "ms.");
  m_status->SetStatisticChanged("Performance", "No. Iterations", std::to_string(m_renderSettings->GetNoIterations()));
}

bool
RenderVkPT::ensureDummyLutTexture()
{
  if (m_dummyLutTexture) {
    return true;
  }

  auto image = m_backend.device().createImage(
    1, 1, 1, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  if (!image) {
    return false;
  }
  auto view = m_backend.device().createImageView(
    image->get(), VK_FORMAT_R8_UNORM, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  if (!view) {
    return false;
  }

  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  auto sampler = m_backend.device().createSampler(samplerInfo);
  if (!sampler) {
    return false;
  }

  transitionImageLayout(m_backend,
                        image->get(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        1);
  m_dummyLutTexture = resources::SampledImage(std::move(*image), std::move(*view), std::move(*sampler));
  return true;
}

bool
RenderVkPT::ensurePtVolumeResources()
{
  VkDevice device = m_backend.logicalDevice();

  if (!ensureDummyLutTexture()) {
    return false;
  }

  if (!m_ptVolumeUniformBuffer) {
    auto buffer = createUniformBuffer(m_backend, sizeof(PtVolumeUniforms));
    if (!buffer) {
      return false;
    }
    m_ptVolumeUniformBuffer = std::move(*buffer);
  }

  if (!m_ptVolumeDescriptorSetLayout) {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1; // volumeTexture (sampler3D)
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2; // g_lutTexture[4] (deprecated, dummy)
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 4;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].binding = 3; // g_colormapTexture (sampler2DArray)
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].binding = 4; // tPreviousTexture (sampler2D)
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    auto descriptorSetLayoutResource = m_backend.device().createDescriptorSetLayout(layoutInfo);
    if (!descriptorSetLayoutResource) {
      return false;
    }
    m_ptVolumeDescriptorSetLayout = std::move(*descriptorSetLayoutResource);

    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 7; // volume(1) + lut(4) + colormap(1) + previous(1)

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    auto descriptorPool = m_backend.device().createDescriptorPool(poolInfo);
    if (!descriptorPool) {
      return false;
    }
    m_ptVolumeDescriptorPool = std::move(*descriptorPool);

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_ptVolumeDescriptorPool.get();
    allocateInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_ptVolumeDescriptorSetLayout.get();
    allocateInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocateInfo, &m_ptVolumeDescriptorSet) != VK_SUCCESS) {
      LOG_ERROR << "vkAllocateDescriptorSets for pathtrace volume failed";
      return false;
    }
  }

  if (m_ptVolumePipeline) {
    return true;
  }

  if (!m_ptVolumeRenderPass) {
    auto renderPass = createColorRenderPass(m_backend, VK_FORMAT_R32G32B32A32_SFLOAT);
    if (!renderPass) {
      return false;
    }
    m_ptVolumeRenderPass = std::move(*renderPass);
  }

  if (!m_ptVolumePipelineLayout) {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout descriptorSetLayout = m_ptVolumeDescriptorSetLayout.get();
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    auto pipelineLayout = m_backend.device().createPipelineLayout(pipelineLayoutInfo);
    if (!pipelineLayout) {
      return false;
    }
    m_ptVolumePipelineLayout = std::move(*pipelineLayout);
  }

  auto vertexShader =
    m_backend.device().createShaderModule(pathTraceVolume_vert_spv, pathTraceVolume_vert_spv_word_count);
  auto fragmentShader =
    m_backend.device().createShaderModule(pathTraceVolume_frag_spv, pathTraceVolume_frag_spv_word_count);
  if (!vertexShader || !fragmentShader) {
    return false;
  }

  VkPipelineShaderStageCreateInfo vertexStage = {};
  vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertexStage.module = vertexShader->get();
  vertexStage.pName = "main";
  VkPipelineShaderStageCreateInfo fragmentStage = {};
  fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragmentStage.module = fragmentShader->get();
  fragmentStage.pName = "main";
  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = { vertexStage, fragmentStage };

  VkVertexInputBindingDescription bindingDescription = {};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(FullscreenVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attributes = {};
  attributes[0].binding = 0;
  attributes[0].location = 0;
  attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset = offsetof(FullscreenVertex, position);
  attributes[1].binding = 0;
  attributes[1].location = 1;
  attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[1].offset = offsetof(FullscreenVertex, uv);

  VkPipelineVertexInputStateCreateInfo vertexInput = {};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &bindingDescription;
  vertexInput.vertexAttributeDescriptionCount = 2;
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
  pipelineInfo.layout = m_ptVolumePipelineLayout.get();
  pipelineInfo.renderPass = m_ptVolumeRenderPass.get();
  pipelineInfo.subpass = 0;

  auto pipeline = m_backend.device().createPipeline(pipelineInfo);
  if (!pipeline) {
    return false;
  }
  m_ptVolumePipeline = std::move(*pipeline);
  return true;
}

} // namespace gfxvulkan
