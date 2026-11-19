#pragma once

#include "ui_agaveGui.h"

#include "Camera.h"
#include "CacheSettings.h"
#include "GLView3D.h"
#include "QRenderSettings.h"
#include "ViewerState.h"
#include "renderDialog.h"

#include "renderlib/AppScene.h"
#include "renderlib/RenderSettings.h"

#include <QMainWindow>
#include <QSlider>

class QAppearanceDockWidget;
class QCameraDockWidget;
class QStatisticsDockWidget;
class QTimelineDockWidget;
class CacheSettingsDockWidget;

class IFileReader;
class ViewToolbar;
struct VolumeDimensions;
struct LoadSpec;

class agaveGui : public QMainWindow
{
  Q_OBJECT

public:
  agaveGui(QWidget* parent = Q_NULLPTR);

  bool open(const std::string& file, const Serialize::ViewerState* vs = nullptr, bool isImageSequence = false);

protected:
  void changeEvent(QEvent* event) override;

private:
  Ui::agaveGuiClass m_ui;

  void onImageLoaded(std::shared_ptr<ImageXYZC> image,
                     const LoadSpec& loadSpec,
                     const VolumeDimensions& dims,
                     const Serialize::ViewerState* vs,
                     std::shared_ptr<IFileReader> reader,
                     // only used if vs is null
                     bool keepCurrentUISettings);

public slots:
  void view_top();
  void view_bottom();
  void view_front();
  void view_back();
  void view_left();
  void view_right();
private slots:
  void open();
  void openDirectory();
  bool openUrl();
  void openJson();
  void openRecentFile();
  void updateRecentFileActions();
  void quit();
  void view_reset();
  void view_frame();
  void view_toggleProjection();
  void showAxisHelper();
  void viewFocusChanged(GLView3D* glView);
  void tabChanged(int index);
  void openMeshDialog();
  void openMesh(const QString& file);
  void saveImage();
  void saveJson();
  void savePython();
  void onRenderAction();
  void OnUpdateRenderer();
  void onAboutDialogAction();
  void onSupportForumAction();
  void onDocumentationAction();
  void onReportBugAction();
  void onSourceCodeAction();
  void onCitationAction();

private:
  enum
  {
    MaxRecentFiles = 8
  };

  Serialize::ViewerState appToViewerState();
  void viewerStateToApp(const Serialize::ViewerState& s);

  // Set the camera's view mode via the viewer window, preserving the
  // lock-lights-to-camera invariant and flagging the camera dirty.
  void setViewMode(EViewMode mode);

  void createActions();
  void createMenus();
  void createToolbars();
  void createDockWindows();

  void showOpenFailedMessageBox(QString path, QString details = "");

  static bool hasRecentFiles();
  void prependToRecentFiles(const QString& fileName);
  void setRecentFilesVisible(bool visible);
  static QString strippedName(const QString& fullFileName);
  void writeRecentDirectory(const QString& directory);
  QString readRecentDirectory();

  QMenu* m_fileMenu;
  QMenu* m_viewMenu;
  QMenu* m_helpMenu;

  QToolBar* m_Cam2DTools;

  QAction* m_openAction = nullptr;
  QAction* m_openJsonAction = nullptr;
  QAction* m_openUrlAction = nullptr;
  QAction* m_openDirectoryAction = nullptr;
  QAction* m_quitAction = nullptr;
  QAction* m_dumpJsonAction = nullptr;
  QAction* m_dumpPythonAction = nullptr;
  QAction* m_testMeshAction = nullptr;
  QAction* m_viewResetAction = nullptr;
  QAction* m_toggleCameraProjectionAction = nullptr;
  QAction* m_saveImageAction = nullptr;
  QAction* m_renderAction = nullptr;
  QAction* m_aboutDialogAction = nullptr;
  QAction* m_supportForumAction = nullptr;
  QAction* m_documentationAction = nullptr;
  QAction* m_reportBugAction = nullptr;
  QAction* m_sourceCodeAction = nullptr;
  QAction* m_citationAction = nullptr;
  QAction* m_toggleRotateControlsAction = nullptr;
  QAction* m_toggleTranslateControlsAction = nullptr;

  QActionGroup* m_manipulatorModeGroup = nullptr;

  QSlider* createAngleSlider();
  QSlider* createRangeSlider();

  // THE camera parameter container
  QCamera m_qcamera;
  // Camera UI
  QCameraDockWidget* m_cameradock;
  // Timeline UI
  QTimelineDockWidget* m_timelinedock;

  QRenderSettings m_qrendersettings;
  QAppearanceDockWidget* m_appearanceDockWidget;

  QStatisticsDockWidget* m_statisticsDockWidget;
  CacheSettingsDockWidget* m_cacheSettingsDockWidget = nullptr;

  QTabWidget* m_tabs;
  GLView3D* m_glView;
  ViewToolbar* m_viewToolbar;
  QWidget* m_viewWithToolbar;

  // THE underlying render settings container.
  // There is only one of these.  The app owns it and hands refs to the ui widgets and the renderer.
  // if renderer is on a separate thread, then this will need a mutex guard
  // any direct programmatic changes to this obj need to be pushed to the UI as well.
  RenderSettings m_renderSettings;

  // the render dialog will modify the contents of this object
  CaptureSettings m_captureSettings;

  // the app owns a scene.
  // scene gets sent down to the renderer.
  Scene m_appScene;
  int m_currentScene = 0;

  CacheSettings m_cacheSettings;

  QAction* m_recentFileActs[MaxRecentFiles];
  QAction* m_recentFileSeparator;
  QAction* m_recentFileSubMenuAct;

  std::string m_currentFilePath;
  // TODO remove the above m_currentFilePath and use this instead
  LoadSpec m_loadSpec;

  Qt::ColorScheme m_colorScheme;
};
