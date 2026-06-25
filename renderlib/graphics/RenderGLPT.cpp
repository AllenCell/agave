#include "RenderGLPT.h"

#include "gfxOpenGL/Backend.h"
#include "glm.h"

#include "Framebuffer.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "gfxOpenGL/FSQ.h"
#include "gfxOpenGL/Image3D.h"
#include "gfxOpenGL/Util.h"
#include "gfxOpenGL/glsl/GLCopyShader.h"
#include "gfxOpenGL/glsl/GLImageShader2DnoLut.h"
#include "gfxOpenGL/glsl/GLPTVolumeShader.h"
#include "gfxOpenGL/glsl/GLToneMapShader.h"

#include <array>

const std::string RenderGLPT::TYPE_NAME = "pathtrace";

RenderGLPT::RenderGLPT(RenderSettings* rs)
  : m_fbF32(nullptr)
  , m_fbF32Accum(nullptr)
  , m_renderBufferShader(nullptr)
  , m_copyShader(nullptr)
  , m_toneMapShader(nullptr)
  , m_fsq(nullptr)
  , m_randomSeeds1(nullptr)
  , m_randomSeeds2(nullptr)
  , m_renderSettings(rs)
  , m_w(0)
  , m_h(0)
  , m_fb(nullptr)
  , m_scene(nullptr)
  , m_gpuBytes(0)
  , m_imagequad(nullptr)
  , m_RandSeed(0)
  , m_status(new CStatus)
{
}

RenderGLPT::~RenderGLPT() {}

void
RenderGLPT::cleanUpFB()
{
  delete m_fb;
  m_fb = nullptr;

  delete m_fbF32;
  m_fbF32 = nullptr;

  delete m_fbF32Accum;
  m_fbF32Accum = nullptr;

  delete m_renderBufferShader;
  m_renderBufferShader = nullptr;
  delete m_copyShader;
  m_copyShader = nullptr;
  delete m_toneMapShader;
  m_toneMapShader = nullptr;
  delete m_fsq;
  m_fsq = nullptr;

  m_gpuBytes = 0;
}

void
RenderGLPT::initFB(uint32_t w, uint32_t h)
{
  cleanUpFB();

  m_fbF32 = new Framebuffer(w, h, GL_RGBA32F);
  m_gpuBytes += sizeof(float) * w * h * 4;
  check_glfb("resized float pathtrace sample fb");

  // clear the newly created FB
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT);

  m_fbF32Accum = new Framebuffer(w, h, GL_RGBA32F);
  m_gpuBytes += sizeof(float) * w * h * 4;
  check_glfb("resized float accumulation fb");

  // clear the newly created FB
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT);

  m_fsq = new FSQ();
  // bottom left is -1,-1, aligned with screen ndc?
  // and bottom left texcoord is therefore 0,0
  m_fsq->setSize(glm::vec2(-1, 1), glm::vec2(-1, 1));
  m_fsq->create();
  m_renderBufferShader = new GLPTVolumeShader();
  m_copyShader = new GLCopyShader();
  m_toneMapShader = new GLToneMapShader();

  {
    unsigned int* pSeeds = (unsigned int*)malloc((size_t)w * (size_t)h * sizeof(unsigned int));
    memset(pSeeds, 0, (size_t)w * (size_t)h * sizeof(unsigned int));
    for (unsigned int i = 0; i < w * h; i++)
      pSeeds[i] = rand();
    // m_gpuBytes += w * h * sizeof(unsigned int);
    free(pSeeds);
  }

  m_fb = new Framebuffer(w, h, GL_RGBA8, true);
  m_gpuBytes += (size_t)w * (size_t)h * 4;

  // clear this fb to black
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void
RenderGLPT::initVolumeTextureGpu()
{

  // free the gpu resources of the old image.
  m_imgGpu.deallocGpu();

  if (!m_scene || !m_scene->m_volume) {
    return;
  }

  uint32_t c0, c1, c2, c3;
  m_scene->getFirst4EnabledChannels(c0, c1, c2, c3);

  m_imgGpu.allocGpuInterleaved(m_scene->m_volume.get(), c0, c1, c2, c3);
}

void
RenderGLPT::initialize(uint32_t w, uint32_t h)
{
  m_imagequad = new RectImage2D();

  initVolumeTextureGpu();
  check_gl("init gl volume");

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  check_gl("init gl state");

  // Size viewport
  resize(w, h);
}

void
RenderGLPT::doRender(const CCamera& camera)
{
  if (!m_scene || !m_scene->m_volume) {
    return;
  }

  if (!m_imgGpu.m_VolumeGLTexture || m_renderSettings->m_DirtyFlags.HasFlag(VolumeDirty)) {
    initVolumeTextureGpu();
    // we have set up everything there is to do before rendering
    m_status->SetRenderBegin();
  }

  // Resizing the image canvas requires special attention
  if (m_renderSettings->m_DirtyFlags.HasFlag(FilmResolutionDirty)) {
    m_renderSettings->SetNoIterations(0);
  }

  // Restart the rendering when when the camera, lights and render params are dirty
  if (m_renderSettings->m_DirtyFlags.HasFlag(CameraDirty | LightsDirty | RenderParamsDirty | TransferFunctionDirty |
                                             RoiDirty)) {
    if (m_renderSettings->m_DirtyFlags.HasFlag(RenderParamsDirty)) {
      // update volume texture sampling state
      m_imgGpu.setVolumeTextureFiltering(m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling);
    }
    if (m_renderSettings->m_DirtyFlags.HasFlag(TransferFunctionDirty)) {
      // TODO: only update the ones that changed.
      int NC = m_scene->m_volume->sizeC();
      int activeChannel = 0;
      for (int i = 0; i < NC; ++i) {
        if (m_scene->m_material.m_enabled[i] && activeChannel < MAX_GL_CHANNELS) {
          m_imgGpu.updateLutGpu(i, m_scene->m_volume.get());
          activeChannel++;
        }
      }
      uint32_t c0, c1, c2, c3;
      m_scene->getFirst4EnabledChannels(c0, c1, c2, c3);
      m_imgGpu.updateLutGPU(m_scene->m_volume.get(), c0, c1, c2, c3, m_scene->m_material);
    }

    //		ResetRenderCanvasView();

    // Reset no. iterations
    m_renderSettings->SetNoIterations(0);
  }
  if (m_renderSettings->m_DirtyFlags.HasFlag(LightsDirty)) {
    for (int i = 0; i < m_scene->m_lighting.m_NoLights; ++i) {
      m_scene->m_lighting.m_Lights[i]->Update(m_scene->m_boundingBox);
    }

    // Reset no. iterations
    m_renderSettings->SetNoIterations(0);
  }
  if (m_renderSettings->m_DirtyFlags.HasFlag(VolumeDataDirty)) {
    uint32_t c0, c1, c2, c3;
    m_scene->getFirst4EnabledChannels(c0, c1, c2, c3);
    m_imgGpu.updateVolumeData4x16(m_scene->m_volume.get(), c0, c1, c2, c3);
    m_renderSettings->SetNoIterations(0);
  }
  // At this point, all dirty flags should have been taken care of, since the flags in the original scene are now
  // cleared
  m_renderSettings->m_DirtyFlags.ClearAllFlags();

  m_renderSettings->m_RenderSettings.m_GradientDelta = 1.0f / (float)this->m_scene->m_volume->maxPixelDimension();

  m_renderSettings->m_DenoiseParams.SetWindowRadius(3);

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

  int numIterations = m_renderSettings->GetNoIterations();

  glm::mat4 m(1.0);

  GLuint accumTargetTex = m_fbF32->colorTextureId(); // the texture of m_fbF32
  GLuint prevAccumTargetTex =
    m_fbF32Accum->colorTextureId(); // the texture that will be tonemapped to screen, a copy of m_fbF32

  GLint drawFboId = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboId);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  for (int i = 0; i < camera.m_Film.m_ExposureIterations; ++i) {
    GLTimer TmrRender;

    // 1. draw pathtrace pass and accumulate, using prevAccumTargetTex as previous accumulation
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbF32->id());
    glViewport(0, 0, m_w, m_h);

    check_glfb("bind framebuffer for pathtrace iteration");

    m_renderBufferShader->bind();

    m_renderBufferShader->setShadingUniforms(m_scene,
                                             m_renderSettings->m_DenoiseParams,
                                             camera,
                                             b,
                                             m_renderSettings->m_RenderSettings,
                                             numIterations,
                                             m_RandSeed,
                                             m_w,
                                             m_h,
                                             m_imgGpu,
                                             prevAccumTargetTex);

    m_fsq->render(m);

    m_timingRender.AddDuration(TmrRender.ElapsedTime());

    // unbind the prevAccumTargetTex
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2. copy to accumTargetTex texture that will be used as accumulator for next pass

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbF32Accum->id());
    check_glfb("bind framebuffer for accumulator");

    // the sample
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbF32->colorTextureId());

    m_copyShader->bind();
    m_copyShader->setShadingUniforms();

    m_fsq->render(m);

    m_copyShader->release();
    //_timingPostProcess.AddDuration(TmrPostProcess.ElapsedTime());

    // ping pong accum buffer. this will stall till previous accum render is done.

    numIterations++;
    m_RandSeed++;
  }

  m_renderSettings->SetNoIterations(numIterations);

  // TODO do denoising in GLSL

  // set the lerpC here because the Render call is incrementing the number of iterations.
  // m_renderSettings->m_DenoiseParams.m_LerpC = 0.33f * (max((float)m_renderSettings->GetNoIterations(), 1.0f)
  // * 1.0f);//1.0f - powf(1.0f / (float)gScene.GetNoIterations(), 15.0f);//1.0f - expf(-0.01f *
  // (float)gScene.GetNoIterations());
  m_renderSettings->m_DenoiseParams.m_LerpC =
    0.33f * (std::max((float)m_renderSettings->GetNoIterations(), 1.0f) * 0.035f);
// 1.0f - powf(1.0f / (float)gScene.GetNoIterations(), 15.0f);//1.0f - expf(-0.01f *
// (float)gScene.GetNoIterations());
//	LOG_DEBUG << "Window " << _w << " " << _h << " Cam " << m_renderSettings->m_Camera.m_Film.m_Resolution.GetResX()
//<< " " << m_renderSettings->m_Camera.m_Film.m_Resolution.GetResY();
#if 0 
  if (m_renderSettings->m_DenoiseParams.m_Enabled && m_renderSettings->m_DenoiseParams.m_LerpC > 0.0f &&
      m_renderSettings->m_DenoiseParams.m_LerpC < 1.0f) {
    // draw from accum buffer into fbtex
    // Denoise(_F32AccumBuffer, _fbTex, _w, _h, m_renderSettings->m_DenoiseParams.m_LerpC);
  } else {
    // ToneMap(_F32AccumBuffer, _fbTex, _w, _h);
  }
#endif
  //_timingDenoise.AddDuration(TmrDenoise.ElapsedTime());

  // Composite into final frame:
  // draw back of bounding box
  // draw volume
  // draw front of bounding box

  glm::vec4 bboxColor(m_scene->m_material.m_boundingBoxColor[0],
                      m_scene->m_material.m_boundingBoxColor[1],
                      m_scene->m_material.m_boundingBoxColor[2],
                      1.0);

  glBindFramebuffer(GL_FRAMEBUFFER, m_fb->id());
  check_glfb("bind framebuffer for tone map");

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_fbF32Accum->colorTextureId());

  // Tonemap into opengl display buffer
  m_toneMapShader->bind();
  m_toneMapShader->setShadingUniforms(1.0f / camera.m_Film.m_Exposure);
  glDepthMask(GL_FALSE);

  m_fsq->render(m);

  m_toneMapShader->release();

  // front of bounding box is now drawn by BoundingBoxTool

  glDisable(GL_BLEND);

  // LOG_DEBUG << "RETURN FROM RENDER";

  // display timings.
  m_status->SetStatisticChanged("Performance", "Render Image", m_timingRender.filteredDurationAsString(), "ms.");
  m_status->SetStatisticChanged("Performance", "Blur Estimate", m_timingBlur.filteredDurationAsString(), "ms.");
  m_status->SetStatisticChanged(
    "Performance", "Post Process Estimate", m_timingPostProcess.filteredDurationAsString(), "ms.");
  m_status->SetStatisticChanged("Performance", "De-noise Image", m_timingDenoise.filteredDurationAsString(), "ms.");

  m_status->SetStatisticChanged("Performance", "No. Iterations", std::to_string(m_renderSettings->GetNoIterations()));

  // restore prior framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, drawFboId);
  check_glfb("bind framebuffer for final draw");

  glEnable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
}

void
RenderGLPT::render(const CCamera& camera)
{
  // draw to m_fbtex
  doRender(camera);

  // put m_fbtex to main render target
  drawImage();
}

void
RenderGLPT::renderTo(const CCamera& camera, GLFramebufferObject* fbo)
{
  doRender(camera);

  // COPY TO MY FBO
  fbo->bind();
  int vw = fbo->width();
  int vh = fbo->height();
  glViewport(0, 0, vw, vh);
  drawImage();
  fbo->release();
}

void
RenderGLPT::drawImage()
{

  glViewport(0, 0, (GLsizei)(m_w), (GLsizei)(m_h));

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // draw quad using the tex that cudaTex was mapped to
  m_imagequad->draw(m_fb->colorTextureId());
}

void
RenderGLPT::resize(uint32_t w, uint32_t h)
{
  // w = 8; h = 8;
  glViewport(0, 0, w, h);
  if ((m_w == w) && (m_h == h)) {
    return;
  }

  initFB(w, h);

  m_w = w;
  m_h = h;

  m_renderSettings->SetNoIterations(0);
}

void
RenderGLPT::cleanUpResources()
{
  m_imgGpu.deallocGpu();

  delete m_imagequad;
  m_imagequad = nullptr;

  cleanUpFB();
}

RenderSettings&
RenderGLPT::renderSettings()
{
  return *m_renderSettings;
}

Scene*
RenderGLPT::scene()
{
  return m_scene;
}

void
RenderGLPT::setScene(Scene* s)
{
  m_scene = s;
}

size_t
RenderGLPT::getGpuBytes()
{
  return m_gpuBytes + m_imgGpu.m_gpuBytes;
}
