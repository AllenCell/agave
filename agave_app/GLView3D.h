#pragma once

#include <memory>

#include "IAppView3D.h"

#include "glm.h"
#include "renderlib/CCamera.h"
#include "renderlib/ViewerWindow.h"
#include "renderlib/gesture/gesture.h"

#include <QOpenGLWidget>
#include <QTimer>

class CStatus;
class ImageXYZC;
class QCamera;
class QRenderSettings;
class Scene;
namespace Serialize {
struct ViewerState;
}

/**
 * 3D GL view of an image with axes and gridlines.
 */
class GLView3D
  : public QOpenGLWidget
  , public IAppView3D
{
  Q_OBJECT

public:
  /**
   * Create a 3D image view.
   *
   * The size and position will be taken from the specified image.
   *
   * @param reader the image reader.
   * @param series the image series.
   * @param parent the parent of this object.
   */
  GLView3D(QCamera* cam, QRenderSettings* qrs, RenderSettings* rs, QWidget* parent = nullptr);

  /// Destructor.
  ~GLView3D() override;

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

  void initCameraFromImage(Scene* scene);
  void retargetCameraForNewVolume(Scene* scene);
  void toggleCameraProjection();
  enum MANIPULATOR_MODE
  {
    NONE,
    ROT,
    TRANS
  };
  void setManipulatorMode(MANIPULATOR_MODE mode);

  void toggleRotateControls();
  void toggleTranslateControls();
  void showRotateControls(bool show);
  void showTranslateControls(bool show);

  void onNewImage(Scene* scene);

  const CCamera& getCamera() { return m_viewerWindow->m_CCamera; }

  void fromViewerState(const Serialize::ViewerState& s);

  QPixmap capture();
  QImage captureQimage();

  QWidget* asWidget() override { return this; }

  // DANGER this must NOT outlive the GLView3D
  ViewerWindow* borrowRenderer() override { return m_viewerWindow; }

  void pauseRenderLoop();
  void restartRenderLoop();

signals:
  void ChangedRenderer();

public slots:

  void OnUpdateCamera();
  void OnUpdateQRenderSettings();
  void OnUpdateRenderer(int);
  void OnSelectionChanged(SceneObject* so);

public:
  std::shared_ptr<CStatus> getStatus();

  /// Resize the view.
  void resizeGL(int w, int h) override;
  void FitToScene(float transitionDurationSeconds = 0.0f);

protected:
  /// Set up GL context and subsidiary objects.
  void initializeGL() override;

  /// Render the scene with the current view settings.
  void paintGL() override;

  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  QCamera* m_qcamera;
  QRenderSettings* m_qrendersettings;

  /// Rendering timer.
  QTimer* m_etimer;

  ViewerWindow* m_viewerWindow;

  MANIPULATOR_MODE m_manipulatorMode = MANIPULATOR_MODE::NONE;
};
