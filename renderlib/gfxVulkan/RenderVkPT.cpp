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

} // namespace gfxvulkan
