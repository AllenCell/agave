#include "RenderGLSlice.h"

#include "AppScene.h"
#include "CacheStatusReport.h"
#include "Enumerations.h"
#include "ImageXYZC.h"
#include "RenderSettings.h"
#include "SliceTransferFunction.h"
#include "gfxOpenGL/FSQ.h"
#include "gfxOpenGL/glsl/GLSliceShader.h"

RenderGLSlice::RenderGLSlice(RenderSettings* renderSettings)
  : m_renderSettings(renderSettings)
  , m_status(new CStatus)
  , m_startTime(std::chrono::high_resolution_clock::now())
{
}

RenderGLSlice::~RenderGLSlice()
{
  cleanUpResources();
}

void
RenderGLSlice::initialize(uint32_t w, uint32_t h)
{
  cleanUpResources();
  m_fullscreenQuad = new FSQ();
  m_fullscreenQuad->setSize(glm::vec2(-1.0f, 1.0f), glm::vec2(-1.0f, 1.0f));
  m_fullscreenQuad->create();
  m_shader = new GLSliceShader();
  initVolumeTexture();
  resize(w, h);
  m_status->SetRenderBegin();
}

void
RenderGLSlice::initVolumeTexture()
{
  m_image.deallocGpu();
  if (!m_scene || !m_scene->m_volume || m_scene->m_volume->sizeC() == 0) {
    return;
  }
  const SliceTransferFunction transfer = buildSliceTransferFunction(*m_scene);
  m_image.allocGpuInterleaved(m_scene->m_volume.get(),
                              transfer.sourceChannels[0],
                              transfer.sourceChannels[1],
                              transfer.sourceChannels[2],
                              transfer.sourceChannels[3]);
  updateActiveColormaps();
  m_image.setVolumeTextureFiltering(m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling);
}

void
RenderGLSlice::updateActiveColormaps()
{
  if (!m_scene || !m_scene->m_volume || !m_image.m_ActiveChannelColormaps) {
    return;
  }
  const SliceTransferFunction transfer = buildSliceTransferFunction(*m_scene);
  m_image.updateLutGPU(m_scene->m_volume.get(),
                       transfer.sourceChannels[0],
                       transfer.sourceChannels[1],
                       transfer.sourceChannels[2],
                       transfer.sourceChannels[3],
                       m_scene->m_material);
}

bool
RenderGLSlice::prepareToRender()
{
  if (!m_scene || !m_scene->m_volume || !m_renderSettings || !m_shader || !m_fullscreenQuad) {
    return false;
  }

  const Flags& dirty = m_renderSettings->m_DirtyFlags;
  const bool fullUpload = !m_image.m_VolumeGLTexture || dirty.HasFlag(VolumeDirty);
  if (fullUpload) {
    initVolumeTexture();
    m_status->SetRenderBegin();
  } else if (dirty.HasFlag(VolumeDataDirty)) {
    const SliceTransferFunction transfer = buildSliceTransferFunction(*m_scene);
    m_image.updateVolumeData4x16(m_scene->m_volume.get(),
                                 transfer.sourceChannels[0],
                                 transfer.sourceChannels[1],
                                 transfer.sourceChannels[2],
                                 transfer.sourceChannels[3]);
    m_status->SetRenderBegin();
  }

  if (!fullUpload && dirty.HasFlag(TransferFunctionDirty)) {
    updateActiveColormaps();
    m_status->SetRenderBegin();
  }
  if (!fullUpload && dirty.HasFlag(RenderParamsDirty)) {
    m_image.setVolumeTextureFiltering(m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling);
  }

  m_renderSettings->m_DirtyFlags.ClearAllFlags();
  return m_image.m_VolumeGLTexture != 0;
}

void
RenderGLSlice::draw(int viewportWidth, int viewportHeight)
{
  if (viewportWidth <= 0 || viewportHeight <= 0) {
    return;
  }
  glViewport(0, 0, viewportWidth, viewportHeight);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  m_shader->bind();
  m_shader->setShadingUniforms(*m_scene, m_renderSettings->m_SliceView, viewportWidth, viewportHeight, m_image);
  m_fullscreenQuad->render(glm::mat4(1.0f));
  m_shader->release();

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, 0);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

void
RenderGLSlice::render(const CCamera&)
{
  if (!prepareToRender()) {
    return;
  }
  draw(static_cast<int>(m_w), static_cast<int>(m_h));

  const auto endTime = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double> elapsed = endTime - m_startTime;
  m_timing.AddDuration(static_cast<float>(elapsed.count() * 1000.0));
  m_status->SetStatisticChanged("Performance", "Render Image", m_timing.filteredDurationAsString(), "ms.");
  reportCacheStatistics(m_status.get());
  m_startTime = endTime;
}

void
RenderGLSlice::renderTo(const CCamera&, gfxApi::Framebuffer* fbo)
{
  if (!fbo || !prepareToRender()) {
    return;
  }
  fbo->bind();
  draw(static_cast<int>(fbo->width()), static_cast<int>(fbo->height()));
  fbo->release();
}

void
RenderGLSlice::resize(uint32_t w, uint32_t h)
{
  m_w = w;
  m_h = h;
}

void
RenderGLSlice::getSize(uint32_t& w, uint32_t& h)
{
  w = m_w;
  h = m_h;
}

void
RenderGLSlice::cleanUpResources()
{
  m_image.deallocGpu();
  delete m_shader;
  m_shader = nullptr;
  delete m_fullscreenQuad;
  m_fullscreenQuad = nullptr;
}

RenderSettings&
RenderGLSlice::renderSettings()
{
  return *m_renderSettings;
}

Scene*
RenderGLSlice::scene()
{
  return m_scene;
}

void
RenderGLSlice::setScene(Scene* scene)
{
  m_scene = scene;
}
