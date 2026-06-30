#pragma once

#if AGAVE_HAS_VULKAN

#include "renderlib/ViewerWindow.h"

#include <vulkan/vulkan.h>

#include <QWidget>

#include <memory>

class CStatus;
class QCamera;
class QRenderSettings;
class QTimer;
class QWindow;
class RenderSettings;
class Scene;

namespace Serialize {
struct ViewerState;
}

class VulkanView3D : public QWidget
{
  Q_OBJECT

public:
  VulkanView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent = nullptr);
  ~VulkanView3D() override;

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

  VkInstance vkInstance() const;
  QWindow* vulkanWindow() const { return m_window; }
  WId nativeWindowId() const;

  void initCameraFromImage(Scene* scene);
  void retargetCameraForNewVolume(Scene* scene);
  void toggleCameraProjection();
  void onNewImage(Scene* scene);
  const CCamera& getCamera() { return m_viewerWindow->m_CCamera; }
  ViewerWindow* borrowRenderer() { return m_viewerWindow.get(); }

  void pauseRenderLoop();
  void restartRenderLoop();

signals:
  void ChangedRenderer();

public slots:
  void OnUpdateCamera();
  void OnUpdateQRenderSettings();
  void OnUpdateRenderer(int rendererType);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void renderFrame();

  QCamera* m_qcamera = nullptr;
  QRenderSettings* m_qrendersettings = nullptr;
  QWindow* m_window = nullptr;
  QWidget* m_container = nullptr;
  QTimer* m_timer = nullptr;
  std::unique_ptr<ViewerWindow> m_viewerWindow;
};

#endif // AGAVE_HAS_VULKAN
