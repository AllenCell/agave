#pragma once

#include "renderlib/CCamera.h"
#include "renderlib/io/FileReader.h"

#include "QtGLContext.h"
#include "renderer.h"

#include <QDialog>
#include <QMutex>
#include <QStandardPaths>

#include <memory>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QImage;
class QLabel;
class QLineEdit;
class QPixmap;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimeEdit;
class QToolBar;
class QWidget;

class RenderRequest;
class RenderSettings;
class Scene;
class ViewerWindow;

namespace gfxApi {
class IRenderWindow;
}

// very simple scroll, zoom, pan and fit image to widget
class ImageDisplay : public QWidget
{
  Q_OBJECT
public:
  ImageDisplay(QWidget* parent = nullptr);
  ~ImageDisplay() override;
  void setImage(QImage* image);

  void save(const QString& filename);

  void scale(qreal s);
  void setScale(qreal s);
  void fit(int w, int h);

private:
  QPixmap* m_pixmap;

  QRectF m_rect;
  QPointF m_reference;
  QPointF m_delta;
  qreal m_scale = 1.0;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent*) override;

protected:
  void paintEvent(QPaintEvent* event) override;
};

class RenderDialog : public QDialog
{
  Q_OBJECT

public:
  RenderDialog(ViewerWindow* borrowedRenderer,
               const RenderSettings& renderSettings,
               const Scene& scene,
               const CCamera& ccamera,
               QtGLContext* glContext,
               const LoadSpec& loadSpec,
               CaptureSettings* captureSettings,
               int viewportWidth,
               int viewportHeight,
               QWidget* parent = Q_NULLPTR);

  void setImage(QImage* image);
  void onZoomFitClicked();

  void done(int r) override;
  int getXResolution();
  int getYResolution();

  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

private slots:
  void render();
  void save();
  void pauseRendering();
  void stopRendering();
  void resumeRendering();
  void onResolutionPreset(int index);
  void updateWidth(const QString& w);
  void updateHeight(const QString& h);
  void updateRenderSamples(int s);
  void updateRenderTime(const QTime& t);
  void onRenderDurationTypeChanged(int index);

private:
  QMutex m_mutex;
  // Borrowed from agaveGui, which owns it and outlives this dialog.
  QtGLContext* m_glContext;
  Renderer* m_renderThread;
  gfxApi::IRenderWindow* m_renderer;
  const RenderSettings& m_renderSettings;
  const Scene& m_scene;
  LoadSpec m_loadSpec;
  // reference that I don't own
  CaptureSettings* mCaptureSettings;
  CCamera m_camera;

  ImageDisplay* mImageView;
  QPushButton* mRenderButton;
  QPushButton* mPauseRenderButton;
  QPushButton* mStopRenderButton;
  QPushButton* mCloseButton;
  QPushButton* mSaveButton;
  QLineEdit* mWidthInput;
  QLineEdit* mHeightInput;
  QPushButton* mLockAspectRatio;
  QComboBox* mResolutionPresets;
  QSpinBox* mStartTimeInput;
  QSpinBox* mEndTimeInput;

  QCheckBox* mAutosaveCheckbox;
  QPushButton* mSelectSaveDirectoryButton;
  QLabel* mSaveDirectoryLabel;
  QLineEdit* mSaveFilePrefix;

  QLabel* mRenderProgressLabel;
  QProgressBar* mFrameProgressBar;
  QProgressBar* mTimeSeriesProgressBar;
  QLabel* mTimeSeriesProgressLabel;
  QButtonGroup* mRenderDurationEdit;
  QStackedWidget* mRenderDurationSettings;
  QSpinBox* mRenderSamplesEdit;
  QTimeEdit* mRenderTimeEdit;
  QPushButton* mZoomInButton;
  QPushButton* mZoomOutButton;
  QPushButton* mZoomFitButton;
  QLabel* mImagePreviewLabel;

  QWidget* mToolbar;
  std::vector<QWidget*> mWidgetsToDisableWhileRendering;

  int mMainViewWidth;
  int mMainViewHeight;
  int mWidth;
  int mHeight;
  float mAspectRatio;
  qint64 m_frameRenderTime;
  qint64 m_uiUpdateTime;

  void endRenderThread();
  void setRenderDurationType(eRenderDurationType type);
  eRenderDurationType mRenderDurationType;
  void resetProgress();

  void onStopButtonClick();

  void onRenderRequestProcessed(RenderRequest* req, QImage image);
  void onFrameDone(QImage image);
  void onZoomInClicked();
  void onZoomOutClicked();

  int mFrameNumber;

  void onStartTimeChanged(int);
  void onEndTimeChanged(int);

  void onSelectSaveDirectoryClicked();
  void onSaveFilePrefixChanged(const QString& value);
  void onRenderThreadFinished();

  QString getFullSavePath();
  QString getUniqueNextFilename(QString path);

  bool getOverwriteConfirmation();
  bool isRenderInProgress();
  bool getUserCancelConfirmation();
  void updateUIReadyToRender();
  void updateUIStartRendering();
  void updateUIStopRendering(bool completed);

  void positionToolbar();
  void updatePreviewImage();
  void updateTimeSeriesProgressLabel();
};
