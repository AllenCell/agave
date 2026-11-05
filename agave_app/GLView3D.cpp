#include "GLView3D.h"

#include "Camera.h"
#include "QRenderSettings.h"
#include "ViewerState.h"

#include "renderlib/AppScene.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/Logging.h"
#include "renderlib/MoveTool.h"
#include "renderlib/RotateTool.h"
#include "renderlib/Status.h"
#include "renderlib/gfxOpenGL/Util.h"

#include <glm.h>

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <cmath>
#include <iostream>

// Only Microsoft issue warnings about correct behaviour...
#ifdef _MSVC_VER
#pragma warning(disable : 4351)
#endif

GLView3D::GLView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent)
  : QOpenGLWidget(parent)
  , m_etimer()
  , m_qcamera(cam)
  , m_viewerWindow(nullptr)
  , m_qrendersettings(qrs)
{
  m_viewerWindow = new ViewerWindow(rs);
  m_viewerWindow->gesture.input.setDoubleClickTime((double)QApplication::doubleClickInterval() / 1000.0);

  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  // The GLView3D owns one CScene

  m_qrendersettings->setRenderSettings(*rs);

  // IMPORTANT this is where the QT gui container classes send their values down into the CScene object.
  // GUI updates --> QT Object Changed() --> cam->Changed() --> GLView3D->OnUpdateCamera
  QObject::connect(cam, SIGNAL(Changed()), this, SLOT(OnUpdateCamera()));
  QObject::connect(qrs, SIGNAL(Changed()), this, SLOT(OnUpdateQRenderSettings()));
  QObject::connect(qrs, SIGNAL(ChangedRenderer(int)), this, SLOT(OnUpdateRenderer(int)));
  QObject::connect(qrs, SIGNAL(Selected(SceneObject*)), this, SLOT(OnSelectionChanged(SceneObject*)));

  // run a timer to update the clock
  // TODO is this different than using this->startTimer and QTimerEvent?
  m_etimer = new QTimer(parent);
  m_etimer->setTimerType(Qt::PreciseTimer);
  connect(m_etimer, &QTimer::timeout, this, [this] {
    // assume that in between QTimer events, true processEvents is called by Qt itself
    // QCoreApplication::processEvents();
    if (isEnabled()) {
      update();
    }
  });
  m_etimer->start();
}

void
GLView3D::initCameraFromImage(Scene* scene)
{
  m_viewerWindow->beginCameraChange();
  // Tell the camera about the volume's bounding box
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MinP = scene->m_boundingBox.GetMinP();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MaxP = scene->m_boundingBox.GetMaxP();
  // reposition to face image
  m_viewerWindow->m_CCamera.SetViewMode(ViewModeFront);
  m_viewerWindow->endCameraChange();

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
GLView3D::retargetCameraForNewVolume(Scene* scene)
{
  // ASSUMPTION camera's sceneboundingbox has not yet been updated!

  glm::vec3 oldctr = m_viewerWindow->m_CCamera.m_SceneBoundingBox.GetCenter();
  // Tell the camera about the volume's bounding box
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MinP = scene->m_boundingBox.GetMinP();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MaxP = scene->m_boundingBox.GetMaxP();
  // reposition target to center of image
  glm::vec3 ctr = m_viewerWindow->m_CCamera.m_SceneBoundingBox.GetCenter();
  // offset target by delta of prev bounds and new bounds.
  m_viewerWindow->m_CCamera.m_Target += (ctr - oldctr);

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
GLView3D::toggleCameraProjection()
{
  ProjectionMode p = m_viewerWindow->m_CCamera.m_Projection;
  m_viewerWindow->m_CCamera.SetProjectionMode((p == PERSPECTIVE) ? ORTHOGRAPHIC : PERSPECTIVE);

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
GLView3D::onNewImage(Scene* scene)
{
  m_viewerWindow->m_renderer->setScene(scene);
  // set volume dirty
  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
  rs->m_DirtyFlags.SetFlag(VolumeDirty);
  rs->m_DirtyFlags.SetFlag(RenderParamsDirty);
  rs->m_DirtyFlags.SetFlag(TransferFunctionDirty);
  rs->m_DirtyFlags.SetFlag(LightsDirty);
}

GLView3D::~GLView3D()
{
  makeCurrent();
  check_gl("view dtor makecurrent");
  // doneCurrent();
}

QSize
GLView3D::minimumSizeHint() const
{
  return QSize(800, 600);
}

QSize
GLView3D::sizeHint() const
{
  return QSize(800, 600);
}

void
GLView3D::initializeGL()
{

  QSize newsize = size();
  float dpr = devicePixelRatioF();
  m_viewerWindow->m_renderer->initialize(newsize.width() * dpr, newsize.height() * dpr);

  // Start timers
  m_etimer->start();

  // Size viewport
  resizeGL(newsize.width(), newsize.height());
}

void
GLView3D::paintGL()
{
  if (!isEnabled()) {
    return;
  }

  m_viewerWindow->redraw();
}

void
GLView3D::resizeGL(int w, int h)
{
  if (!isEnabled()) {
    return;
  }
  // clock tick?
  makeCurrent();
  float dpr = devicePixelRatioF();
  m_viewerWindow->setSize(w * dpr, h * dpr);
  m_viewerWindow->forEachTool(
    [this](ManipulationTool* tool) { tool->setSize(ManipulationTool::s_manipulatorSize * devicePixelRatioF()); });

  doneCurrent();
}

static Gesture::Input::ButtonId
getButton(QMouseEvent* event)
{
  Gesture::Input::ButtonId btn;
  switch (event->button()) {
    case Qt::LeftButton:
      btn = Gesture::Input::ButtonId::kButtonLeft;
      break;
    case Qt::RightButton:
      btn = Gesture::Input::ButtonId::kButtonRight;
      break;
    case Qt::MiddleButton:
      btn = Gesture::Input::ButtonId::kButtonMiddle;
      break;
    default:
      btn = Gesture::Input::ButtonId::kButtonLeft;
      break;
  };
  return btn;
}
static int
getGestureMods(QMouseEvent* event)
{
  int mods = 0;
  if (event->modifiers() & Qt::ShiftModifier) {
    mods |= Gesture::Input::Mods::kShift;
  }
  if (event->modifiers() & Qt::ControlModifier) {
    mods |= Gesture::Input::Mods::kCtrl;
  }
  if (event->modifiers() & Qt::AltModifier) {
    mods |= Gesture::Input::Mods::kAlt;
  }
  if (event->modifiers() & Qt::MetaModifier) {
    mods |= Gesture::Input::Mods::kSuper;
  }
  return mods;
}

void
GLView3D::mousePressEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }

  double time = Clock::now();
  const float dpr = devicePixelRatioF();
  m_viewerWindow->gesture.input.setButtonEvent(getButton(event),
                                               Gesture::Input::Action::kPress,
                                               getGestureMods(event),
                                               glm::vec2(event->x() * dpr, event->y() * dpr),
                                               time);
}

void
GLView3D::mouseReleaseEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }

  double time = Clock::now();
  const float dpr = devicePixelRatioF();
  m_viewerWindow->gesture.input.setButtonEvent(getButton(event),
                                               Gesture::Input::Action::kRelease,
                                               getGestureMods(event),
                                               glm::vec2(event->x() * dpr, event->y() * dpr),
                                               time);
}

// No switch default to avoid -Wunreachable-code errors.
// However, this then makes -Wswitch-default complain.  Disable
// temporarily.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif

void
GLView3D::mouseMoveEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }
  const float dpr = devicePixelRatioF();

  m_viewerWindow->gesture.input.setPointerPosition(glm::vec2(event->x() * dpr, event->y() * dpr));
}

void
GLView3D::wheelEvent(QWheelEvent* event)
{
  if (!isEnabled()) {
    return;
  }
  const float dpr = devicePixelRatioF();

  // tell gesture there was a wheel event
  // m_viewerWindow->gesture.input.setPointerPosition(glm::vec2(event->x() * dpr, event->y() * dpr));
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

void
GLView3D::FitToScene(float transitionDurationSeconds)
{
  Scene* sc = m_viewerWindow->m_renderer->scene();
  if (!sc) {
    return;
  }

  glm::vec3 newPosition, newTarget;
  m_viewerWindow->m_CCamera.ComputeFitToBounds(sc->m_boundingBox, newPosition, newTarget);
  CameraAnimation anim = {};
  anim.duration = transitionDurationSeconds;
  anim.mod.position = newPosition - m_viewerWindow->m_CCamera.m_From;
  anim.mod.target = newTarget - m_viewerWindow->m_CCamera.m_Target;
  m_viewerWindow->m_cameraAnim.push_back(anim);
}

void
GLView3D::OnSelectionChanged(SceneObject* so)
{
  // null ptr is valid here to deselect
  m_viewerWindow->select(so);

  // Toggling the manipulator mode like this
  // has the effect of re-creating the manipulator tool,
  // which will effectively call origins.update to get the new
  // selection into the tool
  MANIPULATOR_MODE mode = m_manipulatorMode;
  setManipulatorMode(MANIPULATOR_MODE::NONE);
  setManipulatorMode(mode);
}

void
GLView3D::setManipulatorMode(MANIPULATOR_MODE mode)
{
  // setting same mode does nothing
  if (m_manipulatorMode == mode) {
    return;
  }
  m_manipulatorMode = mode;
  switch (mode) {
    case MANIPULATOR_MODE::NONE:
      m_viewerWindow->setTool(nullptr);
      break;
    case MANIPULATOR_MODE::ROT:
      m_viewerWindow->setTool(new RotateTool(m_viewerWindow->m_toolsUseLocalSpace,
                                             ManipulationTool::s_manipulatorSize * devicePixelRatioF()));
      m_viewerWindow->forEachTool(
        [this](ManipulationTool* tool) { tool->setUseLocalSpace(m_viewerWindow->m_toolsUseLocalSpace); });
      break;
    case MANIPULATOR_MODE::TRANS:
      m_viewerWindow->setTool(
        new MoveTool(m_viewerWindow->m_toolsUseLocalSpace, ManipulationTool::s_manipulatorSize * devicePixelRatioF()));
      m_viewerWindow->forEachTool(
        [this](ManipulationTool* tool) { tool->setUseLocalSpace(m_viewerWindow->m_toolsUseLocalSpace); });
      break;
    default:
      break;
  }
}

void
GLView3D::showRotateControls(bool show)
{
  if (show) {
    setManipulatorMode(MANIPULATOR_MODE::ROT);
  } else {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  }
}

void
GLView3D::showTranslateControls(bool show)
{
  if (show) {
    setManipulatorMode(MANIPULATOR_MODE::TRANS);
  } else {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  }
}

void
GLView3D::toggleRotateControls()
{
  // if nothing selected, then switch off
  if (!m_viewerWindow->sceneView.getSelectedObject()) {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  }
  // toggle rotate tool
  else if (m_manipulatorMode == MANIPULATOR_MODE::ROT) {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  } else {
    setManipulatorMode(MANIPULATOR_MODE::ROT);
  }
}

// TODO currently this function is not wired up to any gui at all.
// This is because translation of area light source still needs work.
// (Currently rotation is sufficient.)
void
GLView3D::toggleTranslateControls()
{
  // if nothing selected, then switch off
  if (!m_viewerWindow->sceneView.getSelectedObject()) {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  }
  // toggle translate tool
  else if (m_manipulatorMode == MANIPULATOR_MODE::TRANS) {
    setManipulatorMode(MANIPULATOR_MODE::NONE);
  } else {
    setManipulatorMode(MANIPULATOR_MODE::TRANS);
  }
}

void
GLView3D::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_A) {
    FitToScene(0.5f);
  } else if (event->key() == Qt::Key_L) {
    // toggle local/global coordinates for transforms
    m_viewerWindow->m_toolsUseLocalSpace = !m_viewerWindow->m_toolsUseLocalSpace;
    m_viewerWindow->forEachTool(
      [this](ManipulationTool* tool) { tool->setUseLocalSpace(m_viewerWindow->m_toolsUseLocalSpace); });
  } else {
    QOpenGLWidget::keyPressEvent(event);
  }
}

void
GLView3D::OnUpdateCamera()
{
  //	QMutexLocker Locker(&gSceneMutex);
  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  m_viewerWindow->m_CCamera.m_Film.m_Exposure = 1.0f - m_qcamera->GetFilm().GetExposure();
  m_viewerWindow->m_CCamera.m_Film.m_ExposureIterations = m_qcamera->GetFilm().GetExposureIterations();

  if (m_qcamera->GetFilm().IsDirty()) {
    const int FilmWidth = m_qcamera->GetFilm().GetWidth();
    const int FilmHeight = m_qcamera->GetFilm().GetHeight();

    m_viewerWindow->m_CCamera.m_Film.m_Resolution.SetResX(FilmWidth);
    m_viewerWindow->m_CCamera.m_Film.m_Resolution.SetResY(FilmHeight);
    m_viewerWindow->m_CCamera.Update();
    m_qcamera->GetFilm().UnDirty();

    rs->m_DirtyFlags.SetFlag(FilmResolutionDirty);
  }

  m_viewerWindow->m_CCamera.Update();

  // Aperture
  m_viewerWindow->m_CCamera.m_Aperture.m_Size = m_qcamera->GetAperture().GetSize();

  // Projection
  m_viewerWindow->m_CCamera.m_FovV = m_qcamera->GetProjection().GetFieldOfView();

  // Focus
  m_viewerWindow->m_CCamera.m_Focus.m_Type = (Focus::EType)m_qcamera->GetFocus().GetType();
  m_viewerWindow->m_CCamera.m_Focus.m_FocalDistance = m_qcamera->GetFocus().GetFocalDistance();

  rs->m_DenoiseParams.m_Enabled = m_qcamera->GetFilm().GetNoiseReduction();

  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
GLView3D::OnUpdateQRenderSettings()
{
  // QMutexLocker Locker(&gSceneMutex);
  RenderSettings* rs = m_viewerWindow->m_renderSettings;

  rs->m_RenderSettings.m_DensityScale = m_qrendersettings->GetDensityScale();
  rs->m_RenderSettings.m_ShadingType = m_qrendersettings->GetShadingType();
  rs->m_RenderSettings.m_GradientFactor = m_qrendersettings->GetGradientFactor();

  // update window/levels / transfer function here!!!!

  rs->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

std::shared_ptr<CStatus>
GLView3D::getStatus()
{
  return m_viewerWindow->m_renderer->getStatusInterface();
}

void
GLView3D::OnUpdateRenderer(int rendererType)
{
  if (!isEnabled()) {
    LOG_ERROR << "attempted to update GLView3D renderer when view is disabled";
    return;
  }

  makeCurrent();

  m_viewerWindow->setRenderer(rendererType);

  emit ChangedRenderer();
}

void
GLView3D::fromViewerState(const Serialize::ViewerState& s)
{
  m_qrendersettings->SetRendererType(s.rendererType == Serialize::RendererType_PID::PATHTRACE ? 1 : 0);

  // syntactic sugar
  CCamera& camera = m_viewerWindow->m_CCamera;

  camera.m_From = glm::vec3(s.camera.eye[0], s.camera.eye[1], s.camera.eye[2]);
  camera.m_Target = glm::vec3(s.camera.target[0], s.camera.target[1], s.camera.target[2]);
  camera.m_Up = glm::vec3(s.camera.up[0], s.camera.up[1], s.camera.up[2]);
  camera.m_FovV = s.camera.fovY;
  camera.SetProjectionMode(s.camera.projection == Serialize::Projection_PID::PERSPECTIVE ? PERSPECTIVE : ORTHOGRAPHIC);
  camera.m_OrthoScale = s.camera.orthoScale;

  camera.m_Film.m_Exposure = s.camera.exposure;
  camera.m_Aperture.m_Size = s.camera.aperture;
  camera.m_Focus.m_FocalDistance = s.camera.focalDistance;

  // TODO disentangle these QCamera* _camera and CCamera mCamera objects. Only CCamera should be necessary, I think.
  m_qcamera->GetProjection().SetFieldOfView(s.camera.fovY);
  m_qcamera->GetFilm().SetExposure(s.camera.exposure);
  m_qcamera->GetAperture().SetSize(s.camera.aperture);
  m_qcamera->GetFocus().SetFocalDistance(s.camera.focalDistance);
}

QPixmap
GLView3D::capture()
{
  // get the current QScreen
  QScreen* screen = QGuiApplication::primaryScreen();
  if (const QWindow* window = windowHandle()) {
    screen = window->screen();
  }
  if (!screen) {
    qWarning("Couldn't capture screen to save image file.");
    return QPixmap();
  }
  // simply grab the glview window
  return screen->grabWindow(winId());
}

QImage
GLView3D::captureQimage()
{
  if (!isEnabled()) {
    return QImage();
  }

  makeCurrent();

  // Create a one-time FBO to receive the image
  QOpenGLFramebufferObjectFormat fboFormat;
  fboFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
  fboFormat.setMipmap(false);
  fboFormat.setSamples(0);
  fboFormat.setTextureTarget(GL_TEXTURE_2D);

  // NOTE NO ALPHA. if alpha then this will get premultiplied and wash out colors
  // TODO : allow user option for transparent qimage, and then put GL_RGBA8 back here
  fboFormat.setInternalTextureFormat(GL_RGB8);
  check_gl("pre screen capture");

  const float dpr = devicePixelRatioF();
  QOpenGLFramebufferObject* fbo = new QOpenGLFramebufferObject(width() * dpr, height() * dpr, fboFormat);
  check_gl("create fbo");

  fbo->bind();
  check_glfb("bind framebuffer for screen capture");

  // do a render into the temp framebuffer
  glViewport(0, 0, fbo->width(), fbo->height());

  // fill gesture graphics with draw commands
  m_viewerWindow->update(m_viewerWindow->sceneView.viewport, m_viewerWindow->m_clock, m_viewerWindow->gesture);
  m_viewerWindow->m_renderer->render(m_viewerWindow->m_CCamera);
  // render and then clear out draw commands from gesture graphics
  m_viewerWindow->m_gestureRenderer->draw(m_viewerWindow->sceneView, m_viewerWindow->gesture.graphics);

  fbo->release();

  QImage img(fbo->toImage());
  delete fbo;

  return img;
}

void
GLView3D::pauseRenderLoop()
{
  std::shared_ptr<CStatus> s = getStatus();
  // the CStatus updates can cause Qt GUI work to happen,
  // which can not be called from a separate thread.
  // so when we start rendering from another thread,
  // we need to either make status updates thread safe,
  // or just disable them here.
  s->EnableUpdates(false);
  m_etimer->stop();
}

void
GLView3D::restartRenderLoop()
{
  m_etimer->start();
  std::shared_ptr<CStatus> s = getStatus();
  s->EnableUpdates(true);
}
