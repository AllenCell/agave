#include "RenderGL.h"

#include "gfxOpenGL/Backend.h"

#include "ImageXYZC.h"
#include "Logging.h"
#include "RenderSettings.h"
#include "gfxOpenGL/Image3D.h"
#include "gfxOpenGL/Util.h"

#include <iostream>

const std::string RenderGL::TYPE_NAME = "raymarch";

RenderGL::RenderGL(RenderSettings* rs)
  : m_image3d(nullptr)
  , m_w(0)
  , m_h(0)
  , m_renderSettings(rs)
  , m_scene(nullptr)
  , m_status(new CStatus)
{
  mStartTime = std::chrono::high_resolution_clock::now();
}

RenderGL::~RenderGL()
{
  delete m_image3d;
}

void
RenderGL::initialize(uint32_t w, uint32_t h)
{
  GLint max_combined_texture_image_units;
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_combined_texture_image_units);
  LOG_DEBUG << "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: " << max_combined_texture_image_units;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  // glEnable(GL_MULTISAMPLE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (m_scene && m_scene->m_volume) {
    initFromScene();
  }

  // Size viewport
  resize(w, h);
}

// return true if have something to draw
bool
RenderGL::prepareToRender()
{
  if (!m_scene || !m_scene->m_volume) {
    return false;
  }
  if (!m_image3d) {
    initFromScene();
  }

  if (!m_image3d) {
    return false;
  }

  if (m_renderSettings->m_DirtyFlags.HasFlag(RenderParamsDirty | TransferFunctionDirty | VolumeDataDirty)) {
    m_image3d->prepareTexture(*m_scene, m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling);
  }

  // At this point, all dirty flags should have been taken care of, since the flags in the original scene are now
  // cleared
  m_renderSettings->m_DirtyFlags.ClearAllFlags();
  return true;
}

void
RenderGL::renderTo(const CCamera& camera, GLFramebufferObject* fbo)
{
  bool haveScene = prepareToRender();

  // COPY TO MY FBO
  fbo->bind();
  int vw = fbo->width();
  int vh = fbo->height();
  glViewport(0, 0, vw, vh);

  if (haveScene) {
    drawSceneObjects(camera);
  }
  fbo->release();
}

void
RenderGL::drawSceneObjects(const CCamera& camera)
{
  const glm::vec3 volumePhysicalSize = m_scene->m_volume->getPhysicalDimensions();
  float maxPhysicalDim = std::max(volumePhysicalSize.x, std::max(volumePhysicalSize.y, volumePhysicalSize.z));

  // scene bounds are min=0.0, max=image physical dims scaled to max dim so that max dim is 1.0
  glm::vec3 sn = m_scene->m_boundingBox.GetMinP();
  glm::vec3 ext = m_scene->m_boundingBox.GetExtent();
  CBoundingBox b;
  b.SetMinP(glm::vec3(ext.x * m_scene->m_roi.GetMinP().x + sn.x,
                      ext.y * m_scene->m_roi.GetMinP().y + sn.y,
                      ext.z * m_scene->m_roi.GetMinP().z + sn.z));
  b.SetMaxP(glm::vec3(ext.x * m_scene->m_roi.GetMaxP().x + sn.x,
                      ext.y * m_scene->m_roi.GetMaxP().y + sn.y,
                      ext.z * m_scene->m_roi.GetMaxP().z + sn.z));
  // LOG_DEBUG << "CLIPPED BOUNDS" << b.ToString();
  // LOG_DEBUG << "FULL BOUNDS" << m_scene->m_boundingBox.ToString();
  // draw bounding box on top.
  // move the box to match where the camera is pointed
  // transform the box from -1..1 to 0..physicalsize
  float maxd = (std::max)(ext.x, (std::max)(ext.y, ext.z));
  glm::vec3 scales(0.5 * ext.x / maxd, 0.5 * ext.y / maxd, 0.5 * ext.z / maxd);
  // it helps to imagine these transforming the space in reverse order
  // (first translate by 1.0, and then scale down)
  glm::mat4 bboxModelMatrix = glm::scale(glm::mat4(1.0f), scales);
  bboxModelMatrix = glm::translate(bboxModelMatrix, glm::vec3(1.0, 1.0, 1.0));
  glm::mat4 viewMatrix(1.0);
  glm::mat4 projMatrix(1.0);
  camera.getProjMatrix(projMatrix);
  camera.getViewMatrix(viewMatrix);

  m_image3d->render(camera, m_scene, m_renderSettings);
}

void
RenderGL::render(const CCamera& camera)
{
  if (!prepareToRender()) {
    return;
  }

  glViewport(0, 0, (GLsizei)(m_w), (GLsizei)(m_h));
  // Render image
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  drawSceneObjects(camera);

  auto endTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = endTime - mStartTime;
  m_timingRender.AddDuration((float)(elapsed.count() * 1000.0));
  m_status->SetStatisticChanged("Performance", "Render Image", m_timingRender.filteredDurationAsString(), "ms.");
  mStartTime = std::chrono::high_resolution_clock::now();
}

void
RenderGL::resize(uint32_t w, uint32_t h)
{
  m_w = w;
  m_h = h;
  glViewport(0, 0, w, h);
}

RenderSettings&
RenderGL::renderSettings()
{
  return *m_renderSettings;
}
Scene*
RenderGL::scene()
{
  return m_scene;
}
void
RenderGL::setScene(Scene* s)
{
  m_scene = s;
}

void
RenderGL::cleanUpResources()
{
  delete m_image3d;
  m_image3d = nullptr;
}

void
RenderGL::initFromScene()
{
  delete m_image3d;

  m_image3d = new Image3D();
  m_image3d->create(m_scene->m_volume);

  // we have set up everything there is to do before rendering
  mStartTime = std::chrono::high_resolution_clock::now();
  m_status->SetRenderBegin();
}
