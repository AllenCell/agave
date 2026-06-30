#include "VulkanView3D.h"

#if AGAVE_HAS_VULKAN

#include "Camera.h"
#include "QRenderSettings.h"
#include "ViewerState.h"

#include "renderlib/AppScene.h"
#include "renderlib/Logging.h"
#include "renderlib/MoveTool.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/RotateTool.h"
#include "renderlib/Status.h"
#include "renderlib/gfxVulkan/Backend.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/gfxapi/Framebuffer.h"
#include "renderlib/renderlib.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QWheelEvent>

namespace {

gfxApi::ClearColor
backgroundClearColor(const Scene* scene)
{
  if (!scene) {
    return {};
  }

  return { scene->m_material.m_backgroundColor[0],
           scene->m_material.m_backgroundColor[1],
           scene->m_material.m_backgroundColor[2],
           1.0f };
}

Gesture::Input::ButtonId
getButton(QMouseEvent* event)
{
  switch (event->button()) {
    case Qt::LeftButton:
      return Gesture::Input::ButtonId::kButtonLeft;
    case Qt::RightButton:
      return Gesture::Input::ButtonId::kButtonRight;
    case Qt::MiddleButton:
      return Gesture::Input::ButtonId::kButtonMiddle;
    default:
      return Gesture::Input::ButtonId::kButtonLeft;
  }
}

int
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

} // namespace

VulkanView3D::VulkanView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent)
  : QWidget(parent)
  , m_qcamera(cam)
  , m_qrendersettings(qrs)
  , m_viewerWindow(std::make_unique<ViewerWindow>(rs))
{
  // Render directly into this widget's own native surface. Qt lays out a native
  // widget exactly like any other, so the Vulkan content aligns with its place
  // in the layout (unlike a separate QWindow embedded via createWindowContainer,
  // which is offset by sibling/ancestor geometry on macOS).
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(256, 256);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  m_surface = std::make_unique<QtVulkanSurface>(this);
  m_swapchain = std::make_unique<gfxvulkan::Swapchain>(m_surface.get());

  m_viewerWindow->gesture.input.setDoubleClickTime(static_cast<double>(QApplication::doubleClickInterval()) / 1000.0);
  m_qrendersettings->setRenderSettings(*rs);

  QObject::connect(cam, SIGNAL(Changed()), this, SLOT(OnUpdateCamera()));
  QObject::connect(qrs, SIGNAL(Changed()), this, SLOT(OnUpdateQRenderSettings()));
  QObject::connect(qrs, SIGNAL(ChangedRenderer(int)), this, SLOT(OnUpdateRenderer(int)));

  m_timer = new QTimer(this);
  m_timer->setTimerType(Qt::PreciseTimer);
  connect(m_timer, &QTimer::timeout, this, &VulkanView3D::renderFrame);
  m_timer->start();
}

VulkanView3D::~VulkanView3D()
{
  pauseRenderLoop();
}

QSize
VulkanView3D::minimumSizeHint() const
{
  return QSize(800, 600);
}

QSize
VulkanView3D::sizeHint() const
{
  return QSize(800, 600);
}

VkInstance
VulkanView3D::vkInstance() const
{
  gfxApi::Backend* backend = renderlib::graphicsBackend();
  if (!backend || backend->kind() != gfxApi::BackendKind::Vulkan) {
    return VK_NULL_HANDLE;
  }
  return static_cast<gfxvulkan::Backend*>(backend)->instance();
}

void
VulkanView3D::initCameraFromImage(Scene* scene)
{
  m_viewerWindow->beginCameraChange();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MinP = scene->m_boundingBox.GetMinP();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MaxP = scene->m_boundingBox.GetMaxP();
  m_viewerWindow->m_CCamera.SetViewMode(ViewModeFront);
  m_viewerWindow->endCameraChange();

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
VulkanView3D::retargetCameraForNewVolume(Scene* scene)
{
  glm::vec3 oldctr = m_viewerWindow->m_CCamera.m_SceneBoundingBox.GetCenter();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MinP = scene->m_boundingBox.GetMinP();
  m_viewerWindow->m_CCamera.m_SceneBoundingBox.m_MaxP = scene->m_boundingBox.GetMaxP();
  glm::vec3 ctr = m_viewerWindow->m_CCamera.m_SceneBoundingBox.GetCenter();
  m_viewerWindow->m_CCamera.m_Target += (ctr - oldctr);

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
VulkanView3D::toggleCameraProjection()
{
  ProjectionMode p = m_viewerWindow->m_CCamera.m_Projection;
  m_viewerWindow->m_CCamera.SetProjectionMode((p == PERSPECTIVE) ? ORTHOGRAPHIC : PERSPECTIVE);

  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
VulkanView3D::onNewImage(Scene* scene)
{
  m_viewerWindow->m_renderer->setScene(scene);
  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_DirtyFlags.SetFlag(CameraDirty);
  rs->m_DirtyFlags.SetFlag(VolumeDirty);
  rs->m_DirtyFlags.SetFlag(RenderParamsDirty);
  rs->m_DirtyFlags.SetFlag(TransferFunctionDirty);
  rs->m_DirtyFlags.SetFlag(LightsDirty);
}

void
VulkanView3D::setManipulatorMode(MANIPULATOR_MODE mode)
{
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
VulkanView3D::showRotateControls(bool show)
{
  setManipulatorMode(show ? MANIPULATOR_MODE::ROT : MANIPULATOR_MODE::NONE);
}

void
VulkanView3D::showTranslateControls(bool show)
{
  setManipulatorMode(show ? MANIPULATOR_MODE::TRANS : MANIPULATOR_MODE::NONE);
}

void
VulkanView3D::FitToScene(float transitionDurationSeconds)
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
VulkanView3D::fromViewerState(const Serialize::ViewerState& s)
{
  m_qrendersettings->SetRendererType(s.rendererType == Serialize::RendererType_PID::PATHTRACE ? 1 : 0);

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

  m_qcamera->GetProjection().SetFieldOfView(s.camera.fovY);
  m_qcamera->GetFilm().SetExposure(s.camera.exposure);
  m_qcamera->GetAperture().SetSize(s.camera.aperture);
  m_qcamera->GetFocus().SetFocalDistance(s.camera.focalDistance);
}

std::shared_ptr<CStatus>
VulkanView3D::getStatus()
{
  return m_viewerWindow->m_renderer->getStatusInterface();
}

QImage
VulkanView3D::captureQimage()
{
  if (!isEnabled()) {
    return QImage();
  }

  const float dpr = devicePixelRatioF();
  const uint32_t captureWidth = static_cast<uint32_t>(width() * dpr);
  const uint32_t captureHeight = static_cast<uint32_t>(height() * dpr);

  std::unique_ptr<gfxApi::Framebuffer> fbo = renderlib::graphicsBackend()->createFramebuffer(
    { captureWidth, captureHeight, gfxApi::FramebufferColorFormat::Rgba8, true });

  uint32_t rendererWidth = 0;
  uint32_t rendererHeight = 0;
  m_viewerWindow->m_renderer->getSize(rendererWidth, rendererHeight);
  if (rendererWidth != captureWidth || rendererHeight != captureHeight) {
    m_viewerWindow->m_renderer->resize(captureWidth, captureHeight);
  }

  SceneView& sceneView = m_viewerWindow->sceneView;
  sceneView.viewport.region = { { 0, 0 }, { static_cast<int>(captureWidth), static_cast<int>(captureHeight) } };
  sceneView.camera = m_viewerWindow->m_CCamera;
  sceneView.scene = m_viewerWindow->m_renderer->scene();
  sceneView.renderSettings = m_viewerWindow->m_renderSettings;

  m_viewerWindow->m_gestureRenderer->updateSelectionBuffer(captureWidth, captureHeight);
  m_viewerWindow->update(sceneView.viewport, m_viewerWindow->m_clock, m_viewerWindow->gesture);

  fbo->bind();
  fbo->clear(backgroundClearColor(sceneView.scene));
  m_viewerWindow->m_gestureRenderer->drawUnderlay(sceneView, m_viewerWindow->gesture.graphics);
  fbo->release();

  m_viewerWindow->m_renderer->renderTo(sceneView.camera, fbo.get());

  fbo->bind();
  m_viewerWindow->m_gestureRenderer->draw(sceneView, m_viewerWindow->gesture.graphics);
  fbo->release();

  std::unique_ptr<uint8_t> bytes(new uint8_t[captureWidth * captureHeight * 4]);
  fbo->toImage(bytes.get());

  return QImage(bytes.get(), captureWidth, captureHeight, QImage::Format_ARGB32)
    .copy()
    .mirrored()
    .convertToFormat(QImage::Format_RGB32);
}

void
VulkanView3D::pauseRenderLoop()
{
  if (m_timer) {
    m_timer->stop();
  }
}

void
VulkanView3D::restartRenderLoop()
{
  if (m_timer) {
    m_timer->start();
  }
}

void
VulkanView3D::OnUpdateCamera()
{
  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  m_viewerWindow->m_CCamera.m_Film.m_Exposure = 1.0f - m_qcamera->GetFilm().GetExposure();
  m_viewerWindow->m_CCamera.m_Film.m_ExposureIterations = m_qcamera->GetFilm().GetExposureIterations();

  if (m_qcamera->GetFilm().IsDirty()) {
    const int filmWidth = m_qcamera->GetFilm().GetWidth();
    const int filmHeight = m_qcamera->GetFilm().GetHeight();

    m_viewerWindow->m_CCamera.m_Film.m_Resolution.SetResX(filmWidth);
    m_viewerWindow->m_CCamera.m_Film.m_Resolution.SetResY(filmHeight);
    m_viewerWindow->m_CCamera.Update();
    m_qcamera->GetFilm().UnDirty();
    rs->m_DirtyFlags.SetFlag(FilmResolutionDirty);
  }

  m_viewerWindow->m_CCamera.Update();
  m_viewerWindow->m_CCamera.m_Aperture.m_Size = m_qcamera->GetAperture().GetSize();
  m_viewerWindow->m_CCamera.m_FovV = m_qcamera->GetProjection().GetFieldOfView();
  m_viewerWindow->m_CCamera.m_Focus.m_Type = static_cast<Focus::EType>(m_qcamera->GetFocus().GetType());
  m_viewerWindow->m_CCamera.m_Focus.m_FocalDistance = m_qcamera->GetFocus().GetFocalDistance();
  rs->m_DenoiseParams.m_Enabled = m_qcamera->GetFilm().GetNoiseReduction();
  rs->m_DirtyFlags.SetFlag(CameraDirty);
}

void
VulkanView3D::OnUpdateQRenderSettings()
{
  RenderSettings* rs = m_viewerWindow->m_renderSettings;
  rs->m_RenderSettings.m_DensityScale = m_qrendersettings->GetDensityScale();
  rs->m_RenderSettings.m_ShadingType = m_qrendersettings->GetShadingType();
  rs->m_RenderSettings.m_GradientFactor = m_qrendersettings->GetGradientFactor();
  rs->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
VulkanView3D::OnUpdateRenderer(int rendererType)
{
  m_viewerWindow->setRenderer(rendererType);
  emit ChangedRenderer();
}

void
VulkanView3D::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  const float dpr = devicePixelRatioF();
  m_viewerWindow->setSize(static_cast<int>(event->size().width() * dpr), static_cast<int>(event->size().height() * dpr));
  if (m_viewerWindow->m_renderer) {
    m_viewerWindow->m_renderer->resize(m_viewerWindow->width(), m_viewerWindow->height());
  }
  if (m_swapchain) {
    m_swapchain->requestRecreate();
  }
}

void
VulkanView3D::mousePressEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }
  const double time = Clock::now();
  const float dpr = devicePixelRatioF();
  m_viewerWindow->gesture.input.setButtonEvent(getButton(event),
                                               Gesture::Input::Action::kPress,
                                               getGestureMods(event),
                                               glm::vec2(event->position().x() * dpr, event->position().y() * dpr),
                                               time);
}

void
VulkanView3D::mouseReleaseEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }
  const double time = Clock::now();
  const float dpr = devicePixelRatioF();
  m_viewerWindow->gesture.input.setButtonEvent(getButton(event),
                                               Gesture::Input::Action::kRelease,
                                               getGestureMods(event),
                                               glm::vec2(event->position().x() * dpr, event->position().y() * dpr),
                                               time);
}

void
VulkanView3D::mouseMoveEvent(QMouseEvent* event)
{
  if (!isEnabled()) {
    return;
  }
  const float dpr = devicePixelRatioF();
  m_viewerWindow->gesture.input.setPointerPosition(
    glm::vec2(event->position().x() * dpr, event->position().y() * dpr));
}

void
VulkanView3D::wheelEvent(QWheelEvent* event)
{
  (void)event;
}

void
VulkanView3D::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_A) {
    FitToScene(0.5f);
  } else if (event->key() == Qt::Key_L) {
    m_viewerWindow->m_toolsUseLocalSpace = !m_viewerWindow->m_toolsUseLocalSpace;
    m_viewerWindow->forEachTool(
      [this](ManipulationTool* tool) { tool->setUseLocalSpace(m_viewerWindow->m_toolsUseLocalSpace); });
  } else {
    QWidget::keyPressEvent(event);
  }
}

void
VulkanView3D::renderFrame()
{
  if (!isEnabled() || !m_viewerWindow || !m_viewerWindow->m_renderer || !m_swapchain) {
    return;
  }
  m_swapchain->render(*m_viewerWindow);

  // TODO(diagnostic): remove once accumulation is verified. Log the running
  // sample count periodically so we can see whether it grows (accumulating) or
  // is stuck (reset every frame).
  static int s_diagCount = 0;
  if (m_viewerWindow->m_renderSettings && (s_diagCount++ % 60) == 0) {
    LOG_INFO << "VulkanView3D frame " << s_diagCount
             << " NoIterations=" << m_viewerWindow->m_renderSettings->GetNoIterations();
  }
}

#endif // AGAVE_HAS_VULKAN
