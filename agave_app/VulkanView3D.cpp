#include "VulkanView3D.h"

#if AGAVE_HAS_VULKAN

#include "Camera.h"
#include "QRenderSettings.h"

#include "renderlib/AppScene.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/gfxVulkan/Backend.h"
#include "renderlib/renderlib.h"

#include <QApplication>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSurface>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

VulkanView3D::VulkanView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent)
  : QWidget(parent)
  , m_qcamera(cam)
  , m_qrendersettings(qrs)
  , m_viewerWindow(std::make_unique<ViewerWindow>(rs))
{
  m_window = new QWindow();
  m_window->setSurfaceType(QSurface::VulkanSurface);
  m_window->setTitle("AGAVE Vulkan View");
  m_window->create();

  m_container = QWidget::createWindowContainer(m_window, this);
  m_container->setFocusPolicy(Qt::StrongFocus);
  m_container->setMinimumSize(256, 256);
  m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_container);
  setLayout(layout);

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

WId
VulkanView3D::nativeWindowId() const
{
  return m_window ? m_window->winId() : 0;
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
}

void
VulkanView3D::renderFrame()
{
  if (!isEnabled() || !m_viewerWindow || !m_viewerWindow->m_renderer) {
    return;
  }
  m_viewerWindow->redraw();
}

#endif // AGAVE_HAS_VULKAN
