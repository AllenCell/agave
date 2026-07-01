#include "renderer.h"

#include "QtGLContext.h"
#include "renderlib/AppScene.h"
#include "renderlib/BoundingBoxTool.h"
#include "renderlib/CCamera.h"
#include "renderlib/Logging.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/ScaleBarTool.h"
#include "renderlib/SceneView.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/gfxapi/IGLContext.h"
#include "renderlib/gfxapi/IRenderWindow.h"
#include "renderlib/io/FileReader.h"

#include "command.h"
#include "commandBuffer.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QMutexLocker>

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
           0.0f };
}

class MutexContextLocker
{
public:
  explicit MutexContextLocker(QMutex* mutex, gfxApi::IGLContext* context)
    : m_mutex(mutex)
    , m_context(context)
  {
    if (!m_mutex || !m_context) {
      LOG_ERROR << "MutexContextLocker: mutex or context is null";
      return;
    }
    if (m_mutex) {
      m_mutex->lock();
      m_locked = true;
      m_context->makeCurrent();
    }
  }

  ~MutexContextLocker()
  {
    if (m_locked) {
      m_context->doneCurrent();
      m_mutex->unlock();
    }
  }

  // Disable copying
  MutexContextLocker(const MutexContextLocker&) = delete;
  MutexContextLocker& operator=(const MutexContextLocker&) = delete;

private:
  QMutex* m_mutex;
  gfxApi::IGLContext* m_context;
  bool m_locked = false;
};

} // namespace

Renderer::Renderer(const QString& id, QObject* parent, QMutex& mutex)
  : QThread(parent)
  , m_id(id)
  , m_streamMode(false)
  , m_frameIterations(0)
  , m_frameTimeSeconds(0.0f)
  , m_fbo(nullptr)
  , m_width(0)
  , m_height(0)
  , m_openGLMutex(&mutex)
  , m_wait()
{
  this->m_totalQueueDuration = 0;

  LOG_DEBUG << "Renderer " << id.toStdString() << " -- Initializing rendering thread...";
  LOG_DEBUG << "Renderer " << id.toStdString() << " -- Done.";
}

Renderer::~Renderer()
{
  // delete all outstanding requests.
  qDeleteAll(this->m_requests);
}

void
Renderer::configure(gfxApi::IRenderWindow* renderer,
                    const RenderSettings& renderSettings,
                    const Scene& scene,
                    const CCamera& camera,
                    const LoadSpec& loadSpec,
                    // rendererMode ignored if renderer is non-null
                    renderlib::RendererType rendererMode,
                    gfxApi::IGLContext* glContext,
                    const CaptureSettings* captureSettings)
{
  m_ownedGLContext.reset();

  // assumes scene is already set in renderer and everything is initialized
  m_myVolumeData.m_renderSettings = new RenderSettings(renderSettings);
  m_myVolumeData.m_camera = new CCamera(camera);
  // CONTROVERSIAL:
  // it is hard to maintain a full scene copy ctor.
  // do we really have to do this?
  // if we share the scene, what would go wrong?
  m_myVolumeData.m_scene = new Scene(scene);
  m_myVolumeData.m_loadSpec = loadSpec;
  m_myVolumeData.m_captureSettings = captureSettings ? new CaptureSettings(*captureSettings) : new CaptureSettings();
  m_ec.m_loadSpec = loadSpec;
  if (!renderer) {
    m_myVolumeData.m_camera->m_Film.m_Resolution.SetResX(1024);
    m_myVolumeData.m_camera->m_Film.m_Resolution.SetResY(1024);

    m_myVolumeData.ownRenderer = true;
    m_myVolumeData.m_renderer = renderlib::createRenderer(rendererMode, m_myVolumeData.m_renderSettings);
    m_myVolumeData.m_renderer->setScene(m_myVolumeData.m_scene);
  } else {
    m_myVolumeData.ownRenderer = false;
    m_myVolumeData.m_renderer = renderer;
  }

  m_myVolumeData.m_gestureRenderer = renderlib::graphicsBackend()->createGestureRenderer();

  gfxApi::Backend* backend = renderlib::graphicsBackend();
  // Only the OpenGL backend needs a windowing-toolkit context on the render
  // thread. Vulkan's render context is thread-agnostic (RendererVkContext is
  // a no-op wrapper) and does not need one.
  if (backend->kind() == gfxApi::BackendKind::OpenGL && !backend->isHeadless() && !glContext) {
    m_ownedGLContext = std::make_unique<QtGLContext>();
    if (m_ownedGLContext->create()) {
      m_ownedGLContext->moveToThread(this);
      glContext = m_ownedGLContext.get();
    } else {
      LOG_ERROR << "Renderer " << m_id.toStdString() << " failed to create a Qt GL context";
    }
  }

  m_renderContext = backend->createRendererContext(glContext);
}

void
Renderer::init()
{
  if (!m_renderContext || !m_renderContext->create()) {
    LOG_ERROR << "Renderer " << m_id.toStdString() << " failed to create a render GL context";
    return;
  }

  ///////////////////////////////////
  // INIT THE RENDER LIB
  ///////////////////////////////////
  if (m_myVolumeData.ownRenderer) {
    m_myVolumeData.m_renderer->initialize(m_myVolumeData.m_camera->m_Film.m_Resolution.GetResX(),
                                          m_myVolumeData.m_camera->m_Film.m_Resolution.GetResY());
  }

  this->resizeGL(m_myVolumeData.m_camera->m_Film.m_Resolution.GetResX(),
                 m_myVolumeData.m_camera->m_Film.m_Resolution.GetResY());

  reset();

  m_renderContext->doneCurrent();
}

void
Renderer::run()
{
  this->init();

  if (m_renderContext) {
    m_renderContext->makeCurrent();
  }

  while (!this->isInterruptionRequested()) {
    this->processRequest();

    // should be harmless... and maybe handle some signal/slot stuff
    QApplication::processEvents();
  }

  if (m_renderContext) {
    m_renderContext->makeCurrent();
  }
  if (m_myVolumeData.ownRenderer) {
    m_myVolumeData.m_renderer->cleanUpResources();
  }
  shutDown();
}

void
Renderer::wakeUp()
{
  m_wait.wakeAll();
}

void
Renderer::addRequest(RenderRequest* request)
{
  m_requestMutex.lock();

  this->m_requests << request;
  this->m_totalQueueDuration += request->getDuration();
  m_requestMutex.unlock();

  this->m_wait.wakeAll();
}

bool
Renderer::shouldContinue()
{
  // check current frame time against capturesettings.
  if (m_myVolumeData.m_captureSettings) {
    if (m_myVolumeData.m_captureSettings->renderDuration.durationType == SAMPLES) {
      return (m_frameIterations < m_myVolumeData.m_captureSettings->renderDuration.samples);
    } else if (m_myVolumeData.m_captureSettings->renderDuration.durationType == TIME) {
      return (m_frameTimeSeconds < m_myVolumeData.m_captureSettings->renderDuration.duration);
    }
  }
  return true;
}

bool
Renderer::processRequest()
{
  // sleep till request queue has a task
  m_requestMutex.lock();
  if (m_requests.isEmpty()) {
    m_wait.wait(&m_requestMutex);
  }
  if (m_requests.isEmpty()) {
    m_requestMutex.unlock();
    return false;
  }

  RenderRequest* lastReq = nullptr;
  QImage img;
  if (m_streamMode) {
    QElapsedTimer timer;
    timer.start();

    // eat requests until done, and then render
    // note that any one request could change the streaming mode.
    while (!this->m_requests.isEmpty() && m_streamMode && !this->isInterruptionRequested()) {
      RenderRequest* r = this->m_requests.takeFirst();
      this->m_totalQueueDuration -= r->getDuration();

      std::vector<Command*> cmds = r->getParameters();
      if (!cmds.empty()) {
        this->processCommandBuffer(r);
      }

      // the true last request will be passed to "emit" and deleted later
      if (!this->m_requests.isEmpty() && m_streamMode) {
        delete r;
        r = nullptr;
        lastReq = nullptr;
      } else {
        lastReq = r;
      }
    }
    if (lastReq) {
      QWebSocket* ws = lastReq->getClient();
      if (ws /* && ws->isValid() && ws->state() == QAbstractSocket::ConnectedState */) {
        LOG_DEBUG << "RENDER for " << ws->peerName().toStdString() << "(" << ws->peerAddress().toString().toStdString()
                  << ":" << QString::number(ws->peerPort()).toStdString() << ")";
      }

      img = this->render();
      // LOG_DEBUG << "RENDERED sample iteration " << m_frameIterations << " in " << timer.nsecsElapsed() << "ns";

      lastReq->setActualDuration(timer.nsecsElapsed());

      // in stream mode:
      // if queue is empty, then keep firing redraws back to client, to build up iterations.
      m_frameIterations++;
      m_frameTimeSeconds += timer.nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
      if (m_streamMode && shouldContinue()) {
        // push another redraw request.
        std::vector<Command*> cmd;
        RequestRedrawCommandD data;
        cmd.push_back(new RequestRedrawCommand(data));
        RenderRequest* rr = new RenderRequest(ws, cmd, false);

        this->m_requests << rr;
        this->m_totalQueueDuration += rr->getDuration();
      }
    }

  } else {
    // if not in stream mode, then process one request, then re-render.
    if (!this->m_requests.isEmpty() && !this->isInterruptionRequested()) {

      // remove request from the queue
      RenderRequest* r = this->m_requests.takeFirst();
      this->m_totalQueueDuration -= r->getDuration();

      // process it
      QElapsedTimer timer;
      timer.start();

      std::vector<Command*> cmds = r->getParameters();
      if (!cmds.empty()) {
        this->processCommandBuffer(r);
      }

      img = this->render();

      r->setActualDuration(timer.nsecsElapsed());
      lastReq = r;
    }
  }

  // unlock mutex BEFORE emit, in case the signal handler wants to add a request
  m_requestMutex.unlock();

  // inform the server that we are done with r
  // TODO : have a mode where we don't need a QImage
  // and can just return rgba byte array as a thread safe shared ptr
  // writable by render thread and readable by anyone else
  if (!this->isInterruptionRequested()) {
    // TODO look into this way of having the main thread handle this.
    // QMetaObject::invokeMethod(
    //  renderDialog, [=]() { /* ... onRenderRequestProcessed(lastReq, img); ... */ }, Qt::QueuedConnection);
    emit requestProcessed(lastReq, img);

    if (m_streamMode && !shouldContinue()) {
      m_frameIterations = 0;
      m_frameTimeSeconds = 0.0f;
      emit frameDone(img);
    }
  }
  return true;
}

void
Renderer::processCommandBuffer(RenderRequest* rr)
{
  if (m_renderContext) {
    m_renderContext->makeCurrent();
  }

  std::vector<Command*> cmds = rr->getParameters();
  if (!cmds.empty()) {
    m_ec.m_renderSettings = &m_myVolumeData.m_renderer->renderSettings();
    m_ec.m_renderer = this;
    m_ec.m_appScene = m_myVolumeData.m_renderer->scene();
    m_ec.m_camera = m_myVolumeData.m_camera;
    m_ec.m_message = "";

    for (auto i = cmds.begin(); i != cmds.end(); ++i) {
      (*i)->execute(&m_ec);
      // commands can fill in the message field of the ec, and we will send it back to the client
      if (!m_ec.m_message.empty()) {
        emit sendString(rr, QString::fromStdString(m_ec.m_message));
        m_ec.m_message = "";
      }
    }
  }
}

QImage
Renderer::render()
{
  MutexContextLocker locker(m_openGLMutex, m_renderContext.get());

  // DRAW
  m_myVolumeData.m_camera->Update();

  if (!m_myVolumeData.m_gesture.graphics.font.isLoaded()) {
    std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    m_myVolumeData.m_gesture.graphics.font.load(fontPath.c_str());
  }

  SceneView sceneView;
  sceneView.viewport.region = { { 0, 0 }, { m_fbo->width(), m_fbo->height() } };
  sceneView.camera = *(m_myVolumeData.m_camera);
  sceneView.scene = m_myVolumeData.m_renderer->scene();
  sceneView.renderSettings = m_myVolumeData.m_renderSettings;

  // fill gesture graphics with draw commands
  ScaleBarTool scalebar;
  scalebar.clear();
  scalebar.draw(sceneView, m_myVolumeData.m_gesture);
  BoundingBoxTool bbox;
  bbox.clear();
  bbox.draw(sceneView, m_myVolumeData.m_gesture);

  // The gesture renderer needs to know which framebuffer to draw into (Vulkan
  // has no bound/current framebuffer concept). Set it before drawUnderlay/draw.
  m_myVolumeData.m_gestureRenderer->setTargetFramebuffer(m_fbo.get());

  m_fbo->bind();
  m_fbo->clear(backgroundClearColor(sceneView.scene));
  m_myVolumeData.m_gestureRenderer->drawUnderlay(sceneView, m_myVolumeData.m_gesture.graphics);
  m_fbo->release();

  // main scene rendering
  m_myVolumeData.m_renderer->renderTo(sceneView.camera, m_fbo.get());

  m_fbo->bind();
  m_myVolumeData.m_gestureRenderer->draw(sceneView, m_myVolumeData.m_gesture.graphics);
  m_fbo->release();

  std::unique_ptr<uint8_t> bytes(new uint8_t[m_fbo->width() * m_fbo->height() * 4]);
  m_fbo->toImage(bytes.get());
  QImage img = QImage(bytes.get(), m_fbo->width(), m_fbo->height(), QImage::Format_ARGB32).copy();
  // OpenGL framebuffers are bottom-up in memory (glReadPixels origin is
  // bottom-left); Vulkan framebuffers are top-down (top-left origin) and
  // already match QImage's row order.
  if (renderlib::graphicsBackend()->kind() == gfxApi::BackendKind::OpenGL) {
    img = img.mirrored();
  }

  return img;
}

void
Renderer::resizeGL(int width, int height)
{
  if ((width == m_width) && (height == m_height)) {
    return;
  }

  MutexContextLocker locker(m_openGLMutex, m_renderContext.get());

  // RESIZE THE RENDER INTERFACE
  if (m_myVolumeData.m_renderer) {
    m_myVolumeData.m_renderer->resize(width, height);
  }

  this->m_fbo = renderlib::graphicsBackend()->createFramebuffer(
    { static_cast<uint32_t>(width), static_cast<uint32_t>(height), gfxApi::FramebufferColorFormat::Rgba8, true });

  m_width = width;
  m_height = height;
}

void
Renderer::reset(int from)
{
  (void)from;
  this->m_time.start();
}

int
Renderer::getTime()
{
  return this->m_time.elapsed();
}

void
Renderer::shutDown()
{
  {
    MutexContextLocker locker(m_openGLMutex, m_renderContext.get());

    this->m_fbo.reset();
    m_myVolumeData.m_gestureRenderer.reset();

    delete m_myVolumeData.m_captureSettings;
    m_myVolumeData.m_captureSettings = nullptr;

    delete m_myVolumeData.m_renderSettings;
    m_myVolumeData.m_renderSettings = nullptr;

    delete m_myVolumeData.m_camera;
    m_myVolumeData.m_camera = nullptr;

    delete m_myVolumeData.m_scene;
    m_myVolumeData.m_scene = nullptr;

    if (m_myVolumeData.ownRenderer) {
      delete m_myVolumeData.m_renderer;
    }
    m_myVolumeData.m_renderer = nullptr;
  }

  m_renderContext.reset();
  m_ownedGLContext.reset();

  // Stop event processing, move the thread to GUI and make sure it is deleted.
  exit();
  moveToThread(QGuiApplication::instance()->thread());
}
