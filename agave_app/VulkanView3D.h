#pragma once

#if AGAVE_HAS_VULKAN

#include "IAppView3D.h"
#include "QtVulkanSurface.h"

#include "renderlib/ViewerWindow.h"
#include "renderlib/gfxVulkan/Swapchain.h"

#include <vulkan/vulkan.h>

#include <QWidget>

#include <memory>

class CStatus;
class QCamera;
class QPaintEngine;
class QRenderSettings;
class RenderSettings;
class Scene;
class SceneObject;

namespace Serialize {
struct ViewerState;
}

class VulkanView3D
  : public QWidget
  , public IAppView3D
{
  Q_OBJECT

public:
  VulkanView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent = nullptr);
  ~VulkanView3D() override;

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

  // VulkanView3D draws to its own native surface (a CAMetalLayer attached to its
  // NSView), so Qt must not paint over it with the regular backing store.
  QPaintEngine* paintEngine() const override { return nullptr; }

  enum MANIPULATOR_MODE
  {
    NONE,
    ROT,
    TRANS
  };
  void setManipulatorMode(MANIPULATOR_MODE mode);

  QWidget* asWidget() override { return this; }

  void initCameraFromImage(Scene* scene) override;
  void retargetCameraForNewVolume(Scene* scene) override;
  void toggleCameraProjection() override;
  void onNewImage(Scene* scene) override;
  void FitToScene(float transitionDurationSeconds = 0.0f) override;
  void fromViewerState(const Serialize::ViewerState& s) override;
  void showRotateControls(bool show) override;
  void showTranslateControls(bool show) override;
  std::shared_ptr<CStatus> getStatus() override;
  QImage captureQimage() override;
  const CCamera& getCamera() override { return m_viewerWindow->m_CCamera; }
  ViewerWindow* borrowRenderer() override { return m_viewerWindow.get(); }

  void pauseRenderLoop() override;
  void restartRenderLoop() override;

signals:
  void ChangedRenderer();

public slots:
  void OnUpdateCamera();
  void OnUpdateQRenderSettings();
  void OnUpdateRenderer(int rendererType);
  void OnSelectionChanged(SceneObject* so);

protected:
  bool event(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  void renderFrame();
  // Ask the native QWindow to schedule the next UpdateRequest. On desktop
  // platforms this is throttled to the compositor's refresh cadence rather than
  // firing as fast as the event loop can drain -- which is what a 0ms QTimer
  // used to do, and what left no room in the event queue for the pause click.
  void scheduleNextFrame();

  QCamera* m_qcamera = nullptr;
  QRenderSettings* m_qrendersettings = nullptr;
  std::unique_ptr<ViewerWindow> m_viewerWindow;
  std::unique_ptr<QtVulkanSurface> m_surface;
  std::unique_ptr<gfxvulkan::Swapchain> m_swapchain;
  MANIPULATOR_MODE m_manipulatorMode = MANIPULATOR_MODE::NONE;
  bool m_renderPaused = false;
};

#endif // AGAVE_HAS_VULKAN
