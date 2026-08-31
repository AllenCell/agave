#include "agaveGui.h"

#include "QtGLContext.h"
#include "mainwindow.h"
#include "renderlib/CacheManager.h"
#include "renderlib/Logging.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/renderlib.h"
#include "renderlib/version.h"
#include "streamserver.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QUrlQuery>

#include <memory>

constexpr int DEFAULT_SERVER_PORT = 1235;

struct ServerParams
{
  int _port{ DEFAULT_SERVER_PORT };
  QStringList _preloadList;

  // defaults
  ServerParams() = default;
};

ServerParams
readConfig(const QString& configPath)
{
  // defaults from default ctor
  ServerParams p;

  // try to open server.cfg
  QFile loadFile(configPath);
  if (!loadFile.open(QIODevice::ReadOnly)) {
    LOG_INFO << "No server config file found openable at " << configPath.toStdString() << " . Using defaults.";
    return p;
  }

  QByteArray jsonData = loadFile.readAll();
  QJsonDocument jsonDoc(QJsonDocument::fromJson(jsonData));
  if (jsonDoc.isNull()) {
    LOG_INFO << "Invalid server config file format. Make sure it is JSON.";
    return p;
  }

  QJsonObject json(jsonDoc.object());

  // server config file:
  // {
  //   port: 1235,
  //   preload: ['/path/to/file1', '/path/to/file2', ...]
  // }

  if (json.contains("port") /* && json["port"].isDouble()*/) {
    p._port = json["port"].toInt(p._port);
  }

  if (json.contains("preload") && json["preload"].isArray()) {
    QJsonArray preloadArray = json["preload"].toArray();
    p._preloadList.clear();
    p._preloadList.reserve(preloadArray.size());
    for (auto&& i : preloadArray) {
      QString preloadString = i.toString();
      p._preloadList.append(preloadString);
    }
  }

  return p;
}

void
preloadFiles(const QStringList& preloadlist)
{
  for (const QString& s : preloadlist) {
    QFileInfo info(s);
    if (info.exists()) {
      LoadSpec loadSpec;
      loadSpec.filepath = info.absoluteFilePath().toStdString();
      loadSpec.scene = 0;
      loadSpec.time = 0;

      FileReader::loadAndCache(loadSpec);
    } else {
      LOG_INFO << "Could not load " << s.toStdString();
    }
  }
}

void
customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
  // Customize how you handle each message type
  QByteArray localMsg = msg.toLocal8Bit();
  std::string strMsg = localMsg.toStdString();
  switch (type) {
    case QtDebugMsg:
      LOG_DEBUG << strMsg;
      break;
    case QtInfoMsg:
      LOG_INFO << strMsg;
      break;
    case QtWarningMsg:
      LOG_WARNING << strMsg;
      break;
    case QtCriticalMsg:
      LOG_ERROR << strMsg;
      break;
    case QtFatalMsg:
      LOG_FATAL << strMsg;
      abort();
    default:
      LOG_DEBUG << strMsg;
  }
}

std::string
getUrlToOpen(const QUrl& agaveUrl)
{
  if (agaveUrl.isValid()) {
    QUrlQuery query(agaveUrl);
    if (query.hasQueryItem("url")) {
      std::string fileToOpen = query.queryItemValue("url").toStdString();
      return fileToOpen;
    }
  }
  QString urlString = agaveUrl.toString();
  LOG_WARNING << "Received invalid url/file path: " << urlString.toStdString();
  return "";
}

// Platform-appropriate cache directory for AGAVE's volume cache. Cache files
// are not user data, so the location is fixed (not user-configurable) and
// resolved once at startup, the same way the assets path is.
std::string
getCacheDirectory()
{
  QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (baseDir.isEmpty()) {
    baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (baseDir.isEmpty()) {
    baseDir = QDir::currentPath();
  }
  return QDir(baseDir).filePath("agave-cache").toStdString();
}

class AgaveApplication : public QApplication
{
public:
  AgaveApplication(int& argc, char** argv)
    : QApplication(argc, argv)
  {
  }

  void setGUI(agaveGui* gui) { m_gui = gui; }

  bool event(QEvent* event) override
  {
    if (event->type() == QEvent::FileOpen) {
      // This is how MacOS sends file open events, e.g. from a registered agave:// url handler
      QFileOpenEvent* openEvent = static_cast<QFileOpenEvent*>(event);
      QUrl url = openEvent->url();
      std::string fileToOpen = getUrlToOpen(url);
      if (!fileToOpen.empty()) {
        m_gui->open(fileToOpen);
      } else {
        QString urlString = url.toString();
        LOG_WARNING << "Received QFileOpenEvent with invalid url/file path: " << urlString.toStdString();
      }
    }
    return QApplication::event(event);
  }
  agaveGui* m_gui = nullptr;
};

int
main(int argc, char* argv[])
{
  int result = 0;

  try {

    Logging::Init();

    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setStyle("fusion");
    QtGLContext::setDefaultSurfaceFormat();
    AgaveApplication a(argc, argv);
    AgaveApplication::setOrganizationName("Allen Institute");
    AgaveApplication::setOrganizationDomain("allencell.org");
    AgaveApplication::setApplicationName("AGAVE");
    AgaveApplication::setApplicationVersion(AICS_VERSION_STRING);

    LOG_INFO << AgaveApplication::organizationName().toStdString() << " "
             << AgaveApplication::applicationName().toStdString() << " "
             << AgaveApplication::applicationVersion().toStdString();

    QCommandLineParser parser;
    parser.setApplicationDescription("Advanced GPU Accelerated Volume Explorer");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption serverOption("server",
                                    QCoreApplication::translate("main", "Run as websocket server without GUI."));
    parser.addOption(serverOption);
    QCommandLineOption serverPortOption("port",
                                        QCoreApplication::translate("main", "Specify the port for server mode."),
                                        QCoreApplication::translate("main", "port"),
                                        QString::number(DEFAULT_SERVER_PORT));

    parser.addOption(serverPortOption);
    QCommandLineOption loadOption("load",
                                  QCoreApplication::translate("main", "File or url to load."),
                                  QCoreApplication::translate("main", "fileToLoad"));
    parser.addOption(loadOption);

    QCommandLineOption listDevicesOption(
      "list_devices",
      QCoreApplication::translate(
        "main",
        "Log the known graphics devices and exit. The indices reported are the ones --gpu selects. With the OpenGL "
        "backend this is only valid in --server mode."));
    parser.addOption(listDevicesOption);
#if AGAVE_HAS_VULKAN
    const QString defaultGraphicsBackend = "vulkan";
#else
    const QString defaultGraphicsBackend = "opengl";
#endif
    QCommandLineOption graphicsBackendOption(
      "graphics_backend",
      QCoreApplication::translate("main", "Graphics backend to use: opengl or vulkan."),
      QCoreApplication::translate("main", "backend"),
      defaultGraphicsBackend);
    parser.addOption(graphicsBackendOption);
    // No default value here on purpose: parser.isSet() then distinguishes "the
    // user did not ask for a specific GPU" (auto-select) from "the user asked
    // for index 0", which must be validated and never silently replaced.
    QCommandLineOption selectGpuOption(
      "gpu",
      QCoreApplication::translate(
        "main", "Select GPU/device by index, as listed by --list_devices. Defaults to automatic selection."),
      QCoreApplication::translate("main", "gpu"));
    parser.addOption(selectGpuOption);
    QCommandLineOption serverConfigOption("config",
                                          QCoreApplication::translate("main", "Path to config file."),
                                          QCoreApplication::translate("main", "config"),
                                          QCoreApplication::translate("main", "setup.cfg"));
    parser.addOption(serverConfigOption);

    // Process the actual command line arguments given by the user
    parser.process(a);

    bool isServer = parser.isSet(serverOption);
    bool hasPort = parser.isSet(serverPortOption);
    int port = parser.value(serverPortOption).toInt();
    bool listDevices = parser.isSet(listDevicesOption);
    int selectedGpu = gfxApi::kAutoSelectGpu;
    if (parser.isSet(selectGpuOption)) {
      bool gpuIndexOk = false;
      const int requestedGpu = parser.value(selectGpuOption).toInt(&gpuIndexOk);
      if (!gpuIndexOk || requestedGpu < 0) {
        LOG_ERROR << "Invalid --gpu value: " << parser.value(selectGpuOption).toStdString()
                  << ". Expected a zero-based device index; run with --list_devices to see the valid indices.";
        return 0;
      }
      selectedGpu = requestedGpu;
    }
    gfxApi::BackendKind backendKind =
#if AGAVE_HAS_VULKAN
      gfxApi::BackendKind::Vulkan;
#else
      gfxApi::BackendKind::OpenGL;
#endif
    const QString backendString = parser.value(graphicsBackendOption).toLower();
    if (backendString == "vulkan") {
      backendKind = gfxApi::BackendKind::Vulkan;
    } else if (backendString == "opengl") {
      backendKind = gfxApi::BackendKind::OpenGL;
    } else {
      LOG_ERROR << "Unknown graphics backend: " << backendString.toStdString();
      return 0;
    }
    QString fileInput = parser.value(loadOption);
    std::string fileToLoad;
    static const QString kAgaveUrlPrefix("agave://");
    if (fileInput.startsWith(kAgaveUrlPrefix)) {
      fileToLoad = getUrlToOpen(QUrl(fileInput));
    } else {
      fileToLoad = fileInput.toStdString();
    }

    QString appPath = QCoreApplication::applicationDirPath();
    std::string appPathStr = appPath.toStdString();
    LOG_INFO << "Application path: " << appPathStr;

    // renderlib needs to be told where its assets live
    QString assetsPath =
      QStandardPaths::locate(QStandardPaths::AppLocalDataLocation, "assets", QStandardPaths::LocateDirectory);
    if (assetsPath.isEmpty()) {
      // fallback to relative to application dir
      assetsPath = appPath + "/assets";
    }

    LOG_INFO << "Assets path: " << assetsPath.toStdString();

    std::unique_ptr<QtGLContext> bootstrapGLContext;
    const bool needsWindowedGLContext =
      backendKind == gfxApi::BackendKind::OpenGL && (!isServer || !renderlib::supportsHeadlessRendering());
    if (needsWindowedGLContext) {
      bootstrapGLContext = std::make_unique<QtGLContext>();
      if (!bootstrapGLContext->create()) {
        LOG_ERROR << "Failed to create the bootstrap Qt OpenGL context";
        renderlib::cleanup();
        return 0;
      }
    }

    gfxApi::InitParams initParams;
    initParams.backendKind = backendKind;
    initParams.assetPath = assetsPath.toStdString();
    initParams.headless = isServer;
    initParams.selectedGpu = selectedGpu;
    initParams.windowedContext = bootstrapGLContext.get();
#ifndef NDEBUG
    // Enable graphics API validation/debug output in debug builds.
    initParams.enableDebug = true;
#endif

    if (0 == renderlib::initialize(initParams, listDevices)) {
      renderlib::cleanup();
      return 0;
    }

    // Register the cache directory once for the lifetime of the process, after
    // renderlib has successfully initialized.
    // Note that caching stays inert until a CacheConfig enables it.
    CacheManager::initialize(getCacheDirectory());

    if (isServer) {
      QString configPath = parser.value(serverConfigOption);
      ServerParams p = readConfig(configPath);
      // port from command line overrides config file
      if (hasPort) {
        p._port = port;
      }

      auto server = std::make_unique<StreamServer>(p._port, false, nullptr);
      LOG_INFO << "Created server at working directory:" << QDir::currentPath().toStdString() << " on port " << p._port;
#if 0
      // set to true to show windows, or false to run as a console application
      static constexpr bool gui = false;
      if (gui) {
        MainWindow* _ = new MainWindow(server);
        _->resize(512, 512);
        _->show();
      }
#endif

      // delete logFile;

      // must happen after renderlib init
      preloadFiles(p._preloadList);

      result = a.exec();
    } else {

      qInstallMessageHandler(customMessageHandler);

      auto w = std::make_unique<agaveGui>();
      a.setGUI(w.get());
      w->show();
      if (!fileToLoad.empty()) {
        w->open(fileToLoad);
      }
      result = a.exec();
      a.setGUI(nullptr);
      // Destroy the views, swapchain, and renderers while the graphics backend
      // and its Vulkan instance/device are still alive.
      w.reset();
    }
    renderlib::cleanup();
  } catch (const std::exception& exc) {
    LOG_ERROR << "Exception caught in main: " << exc.what();
    renderlib::cleanup();
  } catch (...) {
    LOG_ERROR << "Exception caught in main";
    renderlib::cleanup();
  }

  return result;
}
