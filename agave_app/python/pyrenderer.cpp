#include "pyrenderer.h"

#include "renderlib/BoundingBoxTool.h"
#include "renderlib/CCamera.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/ScaleBarTool.h"
#include "renderlib/TimeStampTool.h"
#include "renderlib/gfxOpenGL/Backend.h"
#include "renderlib/gfxOpenGL/RendererGLContext.h"
#include "renderlib/graphics/RenderGLPT.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/renderlib.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QOpenGLFramebufferObjectFormat>

OffscreenRenderer::OffscreenRenderer()
  : m_rglContext(static_cast<gfxopengl::Backend&>(*renderlib::graphicsBackend()))
  , m_fbo(nullptr)
  , m_width(0)
  , m_height(0)
{
  LOG_DEBUG << "Initializing renderer for python script";
  this->init();
}

OffscreenRenderer::~OffscreenRenderer()
{
  m_rglContext.makeCurrent();
  m_myVolumeData.m_renderer->cleanUpResources();
  shutDown();
}

void
OffscreenRenderer::myVolumeInit()
{
  m_myVolumeData.m_renderSettings = new RenderSettings();

  m_myVolumeData.m_camera = new CCamera();
  m_myVolumeData.m_camera->m_Film.m_ExposureIterations = 1;
  m_myVolumeData.m_camera->m_Film.m_Resolution.SetResX(m_width);
  m_myVolumeData.m_camera->m_Film.m_Resolution.SetResY(m_height);

  m_myVolumeData.m_scene = new Scene();
  m_myVolumeData.m_scene->initLights();

  // TODO allow for all renderer types (e.g. RendererGL also)
  m_myVolumeData.m_renderer = new RenderGLPT(m_myVolumeData.m_renderSettings);
  m_myVolumeData.m_renderer->initialize(m_width, m_height);
  m_myVolumeData.m_renderer->setScene(m_myVolumeData.m_scene);

  // execution context for commands to run
  m_ec.m_renderSettings = m_myVolumeData.m_renderSettings;
  // RENDERER MUST SUPPORT RESIZEGL AND SETSTREAMMODE; SEE command.cpp
  m_ec.m_renderer = nullptr;
  m_ec.m_appScene = m_myVolumeData.m_scene;
  m_ec.m_camera = m_myVolumeData.m_camera;
  m_ec.m_message = "";
}

void
OffscreenRenderer::init()
{
  LOG_DEBUG << "INIT RENDERER";

  m_rglContext.init();

  this->resizeGL(1024, 1024);

  int MaxSamples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &MaxSamples);
  LOG_INFO << "max samples" << MaxSamples;

  glEnable(GL_MULTISAMPLE);

  reset();

  myVolumeInit();

  m_rglContext.doneCurrent();
}

QImage
OffscreenRenderer::render()
{
  m_rglContext.makeCurrent();

  // DRAW
  m_myVolumeData.m_camera->Update();

  int vw = m_fbo->width();
  int vh = m_fbo->height();

  if (!m_myVolumeData.m_gesture.graphics.font.isLoaded()) {
    std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    m_myVolumeData.m_gesture.graphics.font.load(fontPath.c_str());
  }

  SceneView sceneView;
  sceneView.viewport.region = { { 0, 0 }, { vw, vh } };
  sceneView.camera = *(m_myVolumeData.m_camera);
  sceneView.scene = m_myVolumeData.m_renderer->scene();
  sceneView.renderSettings = m_myVolumeData.m_renderSettings;

  // fill gesture graphics with draw commands
  ScaleBarTool scalebar;
  scalebar.clear();
  scalebar.draw(sceneView, m_myVolumeData.m_gesture);
  BoundingBoxTool bbox;
  bbox.clear();
  bbox.draw(sceneView, m_myVolumeData.m_gesture);
  TimeStampTool timestamp;
  timestamp.clear();
  timestamp.draw(sceneView, m_myVolumeData.m_gesture);

  m_fbo->bind();
  clearFramebuffer(sceneView.scene);
  m_myVolumeData.m_gestureRenderer.drawUnderlay(sceneView, nullptr, m_myVolumeData.m_gesture.graphics);
  m_fbo->release();

  // main scene rendering
  m_myVolumeData.m_renderer->renderTo(sceneView.camera, m_fbo);

  m_fbo->bind();
  m_myVolumeData.m_gestureRenderer.draw(sceneView, nullptr, m_myVolumeData.m_gesture.graphics);
  m_fbo->release();

  std::unique_ptr<uint8_t> bytes(new uint8_t[vw * vh * 4]);
  m_fbo->toImage(bytes.get());
  QImage img = QImage(bytes.get(), vw, vh, QImage::Format_RGB32).copy().mirrored();

  m_rglContext.doneCurrent();
  return img;
}

void
OffscreenRenderer::resizeGL(int width, int height)
{
  if ((width == m_width) && (height == m_height)) {
    return;
  }

  m_rglContext.makeCurrent();

  // RESIZE THE RENDER INTERFACE
  if (m_myVolumeData.m_renderer) {
    m_myVolumeData.m_renderer->resize(width, height);
  }

  delete this->m_fbo;
  this->m_fbo = new GLFramebufferObject(width, height, GL_RGBA8);

  glViewport(0, 0, width, height);

  m_width = width;
  m_height = height;
}

void
OffscreenRenderer::reset(int from)
{
  m_rglContext.makeCurrent();

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glEnable(GL_LINE_SMOOTH);
}

void
OffscreenRenderer::shutDown()
{
  m_rglContext.makeCurrent();
  delete this->m_fbo;

  delete m_myVolumeData.m_renderSettings;
  delete m_myVolumeData.m_camera;
  delete m_myVolumeData.m_scene;
  delete m_myVolumeData.m_renderer;
  m_myVolumeData.m_camera = nullptr;
  m_myVolumeData.m_scene = nullptr;
  m_myVolumeData.m_renderSettings = nullptr;
  m_myVolumeData.m_renderer = nullptr;

  m_rglContext.doneCurrent();
  m_rglContext.destroy();
}

// RenderInterface

// tell server to identify this session?
int
OffscreenRenderer::Session(const std::string& s)
{
  m_session = s;
  return 1;
}
// tell server where files might be (appends to existing)
int
OffscreenRenderer::AssetPath(const std::string&)
{
  return 1;
}
// load a volume
int
OffscreenRenderer::LoadOmeTif(const std::string& s)
{
  LoadOmeTifCommand cmd({ s });
  cmd.execute(&m_ec);
  return 1;
}
// load a volume
int
OffscreenRenderer::LoadVolumeFromFile(const std::string& s, int scene, int time)
{
  LoadVolumeFromFileCommand cmd({ s, scene, time });
  cmd.execute(&m_ec);
  return 1;
}
// change load same volume file, different time index
int
OffscreenRenderer::SetTime(int time)
{
  SetTimeCommand cmd({ time });
  cmd.execute(&m_ec);
  return 1;
}
// set camera pos
int
OffscreenRenderer::Eye(float x, float y, float z)
{
  SetCameraPosCommand cmd({ x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
// set camera target pt
int
OffscreenRenderer::Target(float x, float y, float z)
{
  SetCameraTargetCommand cmd({ x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
// set camera up direction
int
OffscreenRenderer::Up(float x, float y, float z)
{
  SetCameraUpCommand cmd({ x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::Aperture(float x)
{
  SetCameraApertureCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
// perspective(0)/ortho(1), fov(degrees)/orthoscale(world units)
int
OffscreenRenderer::CameraProjection(int32_t t, float x)
{
  SetCameraProjectionCommand cmd({ t, x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::Focaldist(float x)
{
  SetCameraFocalDistanceCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::Exposure(float x)
{
  SetCameraExposureCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::MatDiffuse(int32_t c, float r, float g, float b, float a)
{
  SetDiffuseColorCommand cmd({ c, r, g, b, a });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::MatSpecular(int32_t c, float r, float g, float b, float a)
{
  SetSpecularColorCommand cmd({ c, r, g, b, a });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::MatEmissive(int32_t c, float r, float g, float b, float a)
{
  SetEmissiveColorCommand cmd({ c, r, g, b, a });
  cmd.execute(&m_ec);
  return 1;
}
// set num render iterations
int
OffscreenRenderer::RenderIterations(int32_t x)
{
  SetRenderIterationsCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
// (continuous or on-demand frames)
int
OffscreenRenderer::StreamMode(int32_t)
{
  return 1;
}
// request new image
int
OffscreenRenderer::Redraw()
{
  m_lastRenderedImage = this->render();
  m_lastRenderedImage.save(QString::fromStdString(m_session));
  LOG_DEBUG << "Saved image " << m_session;
  return 1;
}
int
OffscreenRenderer::SetResolution(int32_t x, int32_t y)
{
  m_ec.m_camera->m_Film.m_Resolution.SetResX(x);
  m_ec.m_camera->m_Film.m_Resolution.SetResY(y);
  this->resizeGL(x, y);
  m_ec.m_renderSettings->SetNoIterations(0);

  // SetResolutionCommand cmd({ x, y });
  // cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::Density(float x)
{
  SetDensityCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
// move camera to bound and look at the scene contents
int
OffscreenRenderer::FrameScene()
{
  FrameSceneCommand cmd({});
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::MatGlossiness(int32_t c, float g)
{
  SetGlossinessCommand cmd({ c, g });
  cmd.execute(&m_ec);
  return 1;
}
// channel index, 1/0 for enable/disable
int
OffscreenRenderer::EnableChannel(int32_t c, int32_t e)
{
  EnableChannelCommand cmd({ c, e });
  cmd.execute(&m_ec);
  return 1;
}
// channel index, window, level.  (Do I ever set these independently?)
int
OffscreenRenderer::SetWindowLevel(int32_t c, float w, float l)
{
  SetWindowLevelCommand cmd({ c, w, l });
  cmd.execute(&m_ec);
  return 1;
}
// theta, phi in degrees
int
OffscreenRenderer::OrbitCamera(float t, float p)
{
  OrbitCameraCommand cmd({ t, p });
  cmd.execute(&m_ec);
  return 1;
}
// theta, phi in degrees
int
OffscreenRenderer::TrackballCamera(float t, float p)
{
  TrackballCameraCommand cmd({ t, p });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SkylightTopColor(float r, float g, float b)
{
  SetSkylightTopColorCommand cmd({ r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SkylightMiddleColor(float r, float g, float b)
{
  SetSkylightMiddleColorCommand cmd({ r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SkylightBottomColor(float r, float g, float b)
{
  SetSkylightBottomColorCommand cmd({ r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
// r, theta, phi
int
OffscreenRenderer::LightPos(int32_t i, float x, float y, float z)
{
  SetLightPosCommand cmd({ i, x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::LightColor(int32_t i, float r, float g, float b)
{
  SetLightColorCommand cmd({ i, r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
// x by y size
int
OffscreenRenderer::LightSize(int32_t i, float x, float y)
{
  SetLightSizeCommand cmd({ i, x, y });
  cmd.execute(&m_ec);
  return 1;
}
// xmin, xmax, ymin, ymax, zmin, zmax
int
OffscreenRenderer::SetClipRegion(float x0, float x1, float y0, float y1, float z0, float z1)
{
  SetClipRegionCommand cmd({ x0, x1, y0, y1, z0, z1 });
  cmd.execute(&m_ec);
  return 1;
}
// x, y, z pixel scaling
int
OffscreenRenderer::SetVoxelScale(float x, float y, float z)
{
  SetVoxelScaleCommand cmd({ x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
// channel, method
int
OffscreenRenderer::AutoThreshold(int32_t c, int32_t m)
{
  AutoThresholdCommand cmd({ c, m });
  cmd.execute(&m_ec);
  return 1;
}
// channel index, pct_low, pct_high.  (Do I ever set these independently?)
int
OffscreenRenderer::SetPercentileThreshold(int32_t c, float l, float h)
{
  SetPercentileThresholdCommand cmd({ c, l, h });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::MatOpacity(int32_t c, float x)
{
  SetOpacityCommand cmd({ c, x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetPrimaryRayStepSize(float x)
{
  SetPrimaryRayStepSizeCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetSecondaryRayStepSize(float x)
{
  SetSecondaryRayStepSizeCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::BackgroundColor(float r, float g, float b)
{
  SetBackgroundColorCommand cmd({ r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetBoundingBoxColor(float r, float g, float b)
{
  SetBoundingBoxColorCommand cmd({ r, g, b });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::ShowBoundingBox(int32_t on)
{
  ShowBoundingBoxCommand cmd({ on });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::ShowScaleBar(int32_t on)
{
  ShowScaleBarCommand cmd({ on });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::ShowTimeStamp(int32_t on)
{
  ShowTimeStampCommand cmd({ on });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetIsovalueThreshold(int32_t channel, float isovalue, float isorange)
{
  SetIsovalueThresholdCommand cmd({ channel, isovalue, isorange });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetControlPoints(int32_t channel, std::vector<float> controlPoints)
{
  SetControlPointsCommand cmd({ channel, controlPoints });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetFlipAxis(int32_t x, int32_t y, int32_t z)
{
  SetFlipAxisCommand cmd({ x, y, z });
  cmd.execute(&m_ec);
  return 1;
}
int
OffscreenRenderer::SetInterpolation(int32_t x)
{
  SetInterpolationCommand cmd({ x });
  cmd.execute(&m_ec);
  return 1;
}
