#pragma once

#include <QImage>

#include <memory>

class CCamera;
class CStatus;
class QWidget;
class Scene;
class ViewerWindow;

namespace Serialize {
struct ViewerState;
}

// Backend-agnostic contract for the application's central 3D view. agaveGui
// talks to the view through this interface so that the OpenGL (GLView3D) and
// Vulkan (VulkanView3D) views are interchangeable. Backend-specific concerns
// (e.g. the OpenGL context needed by the offscreen render dialog) are handled
// at the call site via the concrete type, not through this interface.
class IAppView3D
{
public:
  virtual ~IAppView3D() = default;

  // The underlying QWidget, for layout and generic QWidget operations.
  virtual QWidget* asWidget() = 0;

  virtual ViewerWindow* borrowRenderer() = 0;
  virtual const CCamera& getCamera() = 0;

  virtual void initCameraFromImage(Scene* scene) = 0;
  virtual void retargetCameraForNewVolume(Scene* scene) = 0;
  virtual void onNewImage(Scene* scene) = 0;

  virtual void toggleCameraProjection() = 0;
  virtual void FitToScene(float transitionDurationSeconds = 0.0f) = 0;
  virtual void fromViewerState(const Serialize::ViewerState& s) = 0;

  virtual void showRotateControls(bool show) = 0;
  virtual void showTranslateControls(bool show) = 0;

  virtual std::shared_ptr<CStatus> getStatus() = 0;
  virtual QImage captureQimage() = 0;

  virtual void pauseRenderLoop() = 0;
  virtual void restartRenderLoop() = 0;
};
