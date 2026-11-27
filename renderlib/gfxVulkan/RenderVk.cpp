#include "RenderVk.h"

#include "CCamera.h"
#include "Enumerations.h"
#include "Framebuffer.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "MathUtil.h"
#include "RenderSettings.h"
#include "ScenePlane.h"
#include "Timing.h"
#include "VulkanUtil.h"
#include "gfxVulkan/Backend.h"
#include "gfxVulkan/Device.h"
#include "gfxVulkan/shadersrc/volume_frag_spv.hpp"
#include "gfxVulkan/shadersrc/volume_vert_spv.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

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
  0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3,
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

  const bool linearFiltering = m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling;
  const auto& dirty = m_renderSettings->m_DirtyFlags;

  // A full re-upload is only required when the raw voxel data changed, or
  // when the material colors need to be re-baked into the volume (which only
  // happens in FusedRgba8 mode, where transfer-function changes fold color +
  // opacity into the volume itself via Fuse).
  const bool voxelDataDirty = dirty.HasFlag(VolumeDirty | VolumeDataDirty);
  const bool transferDirty = dirty.HasFlag(TransferFunctionDirty);
  const bool renderParamsDirty = dirty.HasFlag(RenderParamsDirty);
  const bool needFullUpload =
    !m_volume.valid() || voxelDataDirty || (transferDirty && volumeTextureMode() == VolumeTextureMode::FusedRgba8);

  if (needFullUpload) {
    if (!m_volume.upload(*m_scene, volumeTextureMode(), linearFiltering)) {
      return false;
    }
    m_status->SetRenderBegin();
  } else {
    // Cheap refreshes: colormap-only reupload for RawRgba16 when the transfer
    // function/colormap changed, and a sampler swap when only the interpolation
    // setting flipped. Both preserve the volume voxel data on the GPU.
    if (transferDirty && m_volume.refreshColormap(*m_scene)) {
      m_status->SetRenderBegin();
    }
    if (renderParamsDirty && m_volume.setLinearFiltering(linearFiltering)) {
      m_status->SetRenderBegin();
    }
  }

  if (usesProgressiveAccumulation() &&
      dirty.HasFlag(CameraDirty | LightsDirty | RenderParamsDirty | TransferFunctionDirty | RoiDirty | VolumeDataDirty |
                    FilmResolutionDirty)) {
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

  VkCommandBuffer commandBuffer = m_backend.beginSingleTimeCommands();
  framebuffer.transitionColorImage(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkFramebufferCreateInfo framebufferInfo = {};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_renderPass.get();
  framebufferInfo.attachmentCount = 1;
  VkImageView attachment = framebuffer.colorImageView();
  framebufferInfo.pAttachments = &attachment;
  framebufferInfo.width = framebuffer.width();
  framebufferInfo.height = framebuffer.height();
  framebufferInfo.layers = 1;

  auto vkFramebuffer = m_backend.device().createFramebuffer(framebufferInfo);
  if (!vkFramebuffer) {
    m_backend.endSingleTimeCommands(commandBuffer);
    return;
  }

  const gfxApi::ClearColor bg = backgroundClearColor();
  VkClearValue clearValue = {};
  clearValue.color = { { bg.r, bg.g, bg.b, bg.a } };

  VkRenderPassBeginInfo renderPassBegin = {};
  renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBegin.renderPass = m_renderPass.get();
  renderPassBegin.framebuffer = vkFramebuffer->get();
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
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.get());
  VkDescriptorSet descriptorSet = m_descriptorSet;
  vkCmdBindDescriptorSets(
    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout.get(), 0, 1, &descriptorSet, 0, nullptr);

  VkDeviceSize offset = 0;
  VkBuffer vertexBuffer = m_vertexBuffer.get();
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
  vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.get(), 0, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);
  m_backend.endSingleTimeCommands(commandBuffer);

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
  if (m_vertexBuffer && m_indexBuffer && m_uniformBuffer) {
    return true;
  }

  destroyFrameResources();

  if (!uploadHostBuffer(
        m_backend, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCubeVertices.data(), kCubeVertices.size(), m_vertexBuffer)) {
    return false;
  }

  if (!uploadHostBuffer(
        m_backend, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kCubeIndices.data(), kCubeIndices.size(), m_indexBuffer)) {
    return false;
  }

  auto uniformBuffer =
    m_backend.device().createBuffer(sizeof(VolumeUniforms),
                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!uniformBuffer) {
    return false;
  }
  m_uniformBuffer = std::move(*uniformBuffer);

  m_indexCount = static_cast<uint32_t>(kCubeIndices.size());
  return true;
}

} // namespace gfxvulkan
