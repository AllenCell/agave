#ifndef RENDERER_H
#define RENDERER_H

#include "renderlib/command.h"
#include "renderlib/gesture/gesture.h"
#include "renderlib/gfxapi/Framebuffer.h"
#include "renderlib/gfxapi/IGLContext.h"
#include "renderlib/gfxapi/IGestureRenderer.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/renderlib.h"
#include "renderrequest.h"

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QStandardPaths>
#include <QThread>
#include <QWaitCondition>

#include <memory>

class commandBuffer;
class CCamera;
class ImageXYZC;
class RenderSettings;
class Scene;

namespace gfxApi {
class IGLContext;
class IRenderWindow;
}

class QtGLContext;

// serialized so permanent?
enum eRenderDurationType
{
  TIME = 0,
  SAMPLES = 1
};

struct RenderDuration
{
  int samples = 1;
  int duration = 0; // in seconds
  eRenderDurationType durationType = SAMPLES;
};

struct CaptureSettings
{
  std::string outputDir;
  std::string filenamePrefix;
  int width;
  int height;
  RenderDuration renderDuration;
  int startTime;
  int endTime;

  CaptureSettings()
  {
    // defaults!
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    outputDir = docs.toStdString();

    filenamePrefix = "frame";
    width = 0;
    height = 0;
    renderDuration.duration = 10;
    renderDuration.samples = 32;
    renderDuration.durationType = SAMPLES;
    startTime = 0;
    endTime = 0;
  }

  CaptureSettings(const CaptureSettings& other)
  {
    outputDir = other.outputDir;
    filenamePrefix = other.filenamePrefix;
    width = other.width;
    height = other.height;
    renderDuration = other.renderDuration;
    startTime = other.startTime;
    endTime = other.endTime;
  }
};

// Renderer runs on a dedicated QThread and drives offscreen GL rendering.
// It is used in two scenarios:
//   - RenderDialog: interactive offline render within the GUI application.
//   - Stream server: headless server mode, no windowed GL context.
// The other rendering entry point is GLView3D, which uses Qt's QOpenGLWidget
// and never passes through this class (Qt manages that context implicitly).
class Renderer
  : public QThread
  , public RendererCommandInterface
{
  Q_OBJECT

public:
  Renderer(const QString& id, QObject* parent, QMutex& mutex);
  ~Renderer() override;

  // Configure a render session before starting the render thread.
  //
  // GL context ownership depends on the glContext argument and backend mode:
  //
  //   glContext provided (RenderDialog, Qt windowed):
  //     The caller (agaveGui) owns a QtGLContext that wraps GLView3D's
  //     QOpenGLContext. Renderer borrows it for the render session; the
  //     underlying QOpenGLContext is owned by the QOpenGLWidget for the
  //     lifetime of the application. The context is moved to this render
  //     thread when rendering starts and moved back to the main thread
  //     when the session ends. m_ownedGLContext is unused.
  //
  //   glContext == nullptr, non-headless (stream server, Qt windowed):
  //     Renderer creates and owns m_ownedGLContext — a fresh QtGLContext
  //     with its own QOpenGLContext and offscreen surface. Lives for the
  //     lifetime of this Renderer.
  //
  //   glContext == nullptr, headless (EGL stream server):
  //     The backend creates a HeadlessGLContext inside createRendererContext().
  //     m_renderContext owns it; m_ownedGLContext is unused. Lives for the
  //     lifetime of this Renderer.
  //
  // In all cases m_renderContext holds the IGLContext used on the render thread.
  void configure(gfxApi::IRenderWindow* renderer,
                 const RenderSettings& renderSettings,
                 const Scene& scene,
                 const CCamera& camera,
                 const LoadSpec& loadSpec,
                 // rendererMode ignored if renderer is non-null
                 renderlib::RendererType rendererMode = renderlib::RendererType_Pathtrace,
                 gfxApi::IGLContext* glContext = nullptr,
                 const CaptureSettings* captureSettings = nullptr);

  void run() override;

  void wakeUp();

  void addRequest(RenderRequest* request);
  bool processRequest();

  inline int getTotalQueueDuration() { return this->m_totalQueueDuration; }

  inline int getRequestCount() { return this->m_requests.count(); }

  // 1 = continuous re-render, 0 = only wait for redraw commands
  void setStreamMode(int32_t mode) override { m_streamMode = mode > 0 ? true : false; }

  void resizeGL(int internalWidth, int internalHeight) override;

protected:
  QString m_id;

  void processCommandBuffer(RenderRequest* rr);
  QImage render();

  void reset(int from = 0);

  int getTime();

  // this is a task queue
  QList<RenderRequest*> m_requests;
  QMutex m_requestMutex;
  QWaitCondition m_wait;

  int m_totalQueueDuration;

  void init();
  void shutDown();

private:
  QMutex* m_openGLMutex;

  // Active GL context on the render thread (see configure() comment above).
  std::unique_ptr<gfxApi::IGLContext> m_renderContext;
  // Owned QtGLContext created when no external context is provided (non-headless only).
  std::unique_ptr<QtGLContext> m_ownedGLContext;

  std::unique_ptr<gfxApi::Framebuffer> m_fbo;

  std::atomic<bool> m_streamMode;

  int32_t m_frameIterations;
  float m_frameTimeSeconds;
  bool shouldContinue();

  int32_t m_width, m_height;

  QElapsedTimer m_time;

  struct myVolumeData
  {
    bool ownRenderer;
    RenderSettings* m_renderSettings;
    CaptureSettings* m_captureSettings;
    gfxApi::IRenderWindow* m_renderer;
    Scene* m_scene;
    CCamera* m_camera;
    LoadSpec m_loadSpec;
    Gesture m_gesture;
    std::unique_ptr<gfxApi::IGestureRenderer> m_gestureRenderer;

    myVolumeData()
      : m_camera(nullptr)
      , m_scene(nullptr)
      , m_renderSettings(nullptr)
      , m_captureSettings(nullptr)
      , m_renderer(nullptr)
      , ownRenderer(false)
    {
    }
  } m_myVolumeData;

  ExecutionContext m_ec;

signals:
  void requestProcessed(RenderRequest* request, QImage img);
  void frameDone(QImage img);
  void sendString(RenderRequest* request, QString s);
};

#endif // RENDERER_H
