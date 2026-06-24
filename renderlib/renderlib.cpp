#include "renderlib.h"

#include "ImageXYZC.h"
#include "ImageXyzcGpu.h"
#include "Logging.h"
#include "RenderGL.h"
#include "RenderGLPT.h"
#include "gfxOpenGL/Backend.h"
#include "gfxapi/Backend.h"

#include <QGuiApplication>
#include <QOpenGLDebugLogger>

#include <string>

#if HAS_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#ifdef __cplusplus
extern "C"
{
#endif

  __declspec(dllexport) DWORD NvOptimusEnablement = 1;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#ifdef __cplusplus
}
#endif
#endif

static bool renderLibInitialized = false;

static bool renderLibHeadless = false;

static std::string s_assetPath = "";

// Owner of the active graphics backend. Hardcoded to OpenGL while the
// gfxapi / gfxOpenGL abstraction is being introduced incrementally.
static std::unique_ptr<gfxApi::Backend> s_graphicsBackend;

// Backend selection lives here, in renderlib, rather than in gfxapi: the
// abstract gfxapi layer must not depend on any concrete backend. This is the
// one place that maps a BackendKind onto a concrete implementation.
static std::unique_ptr<gfxApi::Backend>
createGraphicsBackend(gfxApi::BackendKind kind, const gfxApi::InitParams& params)
{
  switch (kind) {
    case gfxApi::BackendKind::OpenGL:
      return std::make_unique<gfxopengl::Backend>(params);
    case gfxApi::BackendKind::Vulkan:
    case gfxApi::BackendKind::WebGPU:
    default:
      LOG_ERROR << "createGraphicsBackend: requested backend kind is not supported in this build";
      return nullptr;
  }
}

#if HAS_EGL
static EGLDisplay eglDpy = NULL;
#endif

static QOpenGLContext* dummyContext = nullptr;
static QOffscreenSurface* dummySurface = nullptr;

static QOpenGLDebugLogger* logger = nullptr;

std::map<std::shared_ptr<ImageXYZC>, std::shared_ptr<ImageGpu>> renderlib::sGpuImageCache;

static const struct
{
  int major = 4;
  int minor = 1;
} AICS_GL_VERSION;

static const uint32_t AICS_DEFAULT_STENCIL_BUFFER_BITS = 8;

static const uint32_t AICS_DEFAULT_DEPTH_BUFFER_BITS = 24;

namespace {
static void
logMessage(const QOpenGLDebugMessage& message)
{
  LOG_DEBUG << message.message().toStdString();
}
}

QSurfaceFormat
renderlib::getQSurfaceFormat(bool enableDebug)
{
  QSurfaceFormat format;
  format.setDepthBufferSize(AICS_DEFAULT_DEPTH_BUFFER_BITS);
  format.setStencilBufferSize(AICS_DEFAULT_STENCIL_BUFFER_BITS);
  format.setVersion(AICS_GL_VERSION.major, AICS_GL_VERSION.minor);
  // necessary on MacOS at least:
  format.setProfile(QSurfaceFormat::CoreProfile);
  if (enableDebug) {
    format.setOption(QSurfaceFormat::DebugContext);
  }
  return format;
}

QOpenGLContext*
renderlib::createOpenGLContext()
{
  QOpenGLContext* context = new QOpenGLContext();
  context->setFormat(getQSurfaceFormat()); // ...and set the format on the context too

  bool createdOk = context->create();
  if (!createdOk) {
    LOG_ERROR << "Failed to create OpenGL Context";
  } else {
    LOG_INFO << "Created opengl context";
  }
  if (!context->isValid()) {
    LOG_ERROR << "Created GL Context is not valid";
  }

  return context;
}

#if HAS_EGL

void
checkEGLError(std::string message)
{
  EGLint lastError = EGL_SUCCESS;
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
    LOG_ERROR << message;
  }
}

EGLDisplay
getEGLDefaultDisplay()
{
  EGLint lastError = EGL_SUCCESS;
  EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  LOG_INFO << "eglGetDisplay returns " << eglDpy;
  checkEGLError("Failed eglGetDisplay");
  return eglDisplay;
}

EGLDisplay
initEGLDisplay(int selectedGpu)
{
  PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
  checkEGLError("Failed to get EGLEXT: eglQueryDevicesEXT");
  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  checkEGLError("Failed to get EGLEXT: eglGetPlatformDisplayEXT");
  PFNEGLQUERYDEVICEATTRIBEXTPROC eglQueryDeviceAttribEXT =
    (PFNEGLQUERYDEVICEATTRIBEXTPROC)eglGetProcAddress("eglQueryDeviceAttribEXT");
  checkEGLError("Failed to get EGLEXT: eglQueryDeviceAttribEXT");
  PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT =
    (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
  checkEGLError("Failed to get EGLEXT: eglQueryDeviceStringEXT");

  if (!eglQueryDevicesEXT || !eglGetPlatformDisplayEXT || !eglQueryDeviceAttribEXT || !eglQueryDeviceStringEXT) {
    return getEGLDefaultDisplay();
  }

  EGLint numberDevices;
  // Get number of devices
  EGLBoolean ok = eglQueryDevicesEXT(0, NULL, &numberDevices);
  if (!ok) {
    LOG_ERROR << "Failed to get number of devices. Bad parameter suspected";
  }
  checkEGLError("Error getting number of devices: eglQueryDevicesEXT");

  LOG_INFO << numberDevices << " devices found";
  if (numberDevices > 0) {
    EGLDeviceEXT* eglDevs = new EGLDeviceEXT[numberDevices];
    ok = eglQueryDevicesEXT(numberDevices, eglDevs, &numberDevices);
    if (!ok) {
      LOG_ERROR << "Failed to get devices. Bad parameter suspected";
    }
    checkEGLError("Error getting number of devices: eglQueryDevicesEXT");
    for (int i = 0; i < numberDevices; ++i) {
      LOG_INFO << "Device " << i << ":";
#ifdef EGL_VENDOR
      const char* vendorstring = eglQueryDeviceStringEXT(eglDevs[i], EGL_VENDOR);
      checkEGLError("Error retrieving EGL_VENDOR string for device");
      if (vendorstring) {
        LOG_INFO << "  Vendor: " << vendorstring;
      }
#endif
#ifdef EGL_RENDERER_EXT
      const char* rendererstring = eglQueryDeviceStringEXT(eglDevs[i], EGL_RENDERER_EXT);
      checkEGLError("Error retrieving EGL_RENDERER_EXT string for device");
      if (rendererstring) {
        LOG_INFO << "  Renderer: " << rendererstring;
      }
#endif
#ifdef EGL_EXTENSIONS
      const char* extensionsstring = eglQueryDeviceStringEXT(eglDevs[i], EGL_EXTENSIONS);
      checkEGLError("Error retrieving EGL_EXTENSIONS string for device");
      if (extensionsstring) {
        LOG_INFO << "  Extensions: " << extensionsstring;
      }
#endif
    }
    if (selectedGpu >= numberDevices || selectedGpu < 0) {
      LOG_WARNING << "Invalid GPU " << selectedGpu << " requested. Using default gpu.";
      return getEGLDefaultDisplay();
    }
    // select device by index
    EGLDisplay eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, eglDevs[selectedGpu], 0);
    checkEGLError("Error getting Platform Display: eglGetPlatformDisplayEXT");
    return eglDisplay;
  } else {
    return getEGLDefaultDisplay();
  }
}
#endif

int
renderlib::initialize(std::string assetPath, bool headless, bool listDevices, int selectedGpu)
{
  if (renderLibInitialized) {
    return 1;
  }
  renderLibInitialized = true;
  s_assetPath = assetPath;

// no MACOS support for EGL
#if HAS_EGL
#else
  headless = false;
#endif
  renderLibHeadless = headless;

  LOG_INFO << "Renderlib startup";

  // TODO: backend selection. For now the only supported gfxapi backend is
  // OpenGL; create it unconditionally so renderer code can begin migrating
  // through the backend's device(). The GL context itself is still
  // initialized below via Qt / EGL.
  s_graphicsBackend = createGraphicsBackend(gfxApi::BackendKind::OpenGL, gfxApi::InitParams{ assetPath, headless, selectedGpu });

  bool enableDebug = false;

  QSurfaceFormat format = getQSurfaceFormat();
  QSurfaceFormat::setDefaultFormat(format);

  HeadlessGLContext* dummyHeadlessContext = nullptr;

  if (headless) {
#if HAS_EGL

    // one-time EGL init

    EGLint lastError = EGL_SUCCESS;

    // 1. Initialize EGL
    eglDpy = initEGLDisplay(selectedGpu);

    if (listDevices) {
      return 0;
    }

    EGLint major, minor;

    EGLBoolean init_ok = eglInitialize(eglDpy, &major, &minor);
    if (init_ok == EGL_FALSE) {
      LOG_ERROR << "renderlib::initialize, eglInitialize failed";
    }
    if ((lastError = eglGetError()) != EGL_SUCCESS) {
      LOG_ERROR << "eglGetError " << lastError;
    }
    // 2. Bind the API
    EGLBoolean bindapi_ok = eglBindAPI(EGL_OPENGL_API);
    if (bindapi_ok == EGL_FALSE) {
      LOG_ERROR << "renderlib::initialize, eglBindAPI failed";
    }
    if ((lastError = eglGetError()) != EGL_SUCCESS) {
      LOG_ERROR << "eglGetError " << lastError;
    }
    dummyHeadlessContext = new HeadlessGLContext();
    dummyHeadlessContext->makeCurrent();
#else
    LOG_ERROR << "Headless operation without EGL support is not available";
#endif
  } else {
    dummyContext = renderlib::createOpenGLContext();

    dummySurface = new QOffscreenSurface();
    dummySurface->setFormat(dummyContext->format());
    dummySurface->create();
    LOG_INFO << "Created offscreen surface";
    if (!dummySurface->isValid()) {
      LOG_ERROR << "QOffscreenSurface is not valid";
    }

    bool ok = dummyContext->makeCurrent(dummySurface);
    if (!ok) {
      LOG_ERROR << "Failed to makeCurrent on offscreen surface";
    } else {
      LOG_INFO << "Made context current on offscreen surface";
    }
  }

  if (enableDebug) {
    logger = new QOpenGLDebugLogger();
    QObject::connect(logger, &QOpenGLDebugLogger::messageLogged, logMessage);
    if (logger->initialize()) {
      logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
      logger->enableMessages();
    }
  }

  // note: there MUST be a valid current gl context in order to run this:
  int status = gladLoadGL();
  if (!status) {
    LOG_ERROR << "Failed to init GL";
    return status;
  }

  LOG_INFO << "GL_VENDOR: " << std::string((char*)glGetString(GL_VENDOR));
  LOG_INFO << "GL_RENDERER: " << std::string((char*)glGetString(GL_RENDERER));

  delete dummyHeadlessContext;
  return status;
}

std::string
renderlib::assetPath()
{
  return s_assetPath;
}

void
renderlib::clearGpuVolumeCache()
{
  // clean up the shared gpu buffer cache
  for (auto i : sGpuImageCache) {
    i.second->deallocGpu();
  }
  sGpuImageCache.clear();
}

void
renderlib::cleanup()
{
  if (!renderLibInitialized) {
    return;
  }
  LOG_INFO << "Renderlib shutdown";

  clearGpuVolumeCache();

  delete dummySurface;
  dummySurface = nullptr;
  delete dummyContext;
  dummyContext = nullptr;
  delete logger;
  logger = nullptr;

  if (renderLibHeadless) {
#if HAS_EGL
    eglTerminate(eglDpy);
#endif
  }

  s_graphicsBackend.reset();

  renderLibInitialized = false;
}

std::shared_ptr<ImageGpu>
renderlib::imageAllocGPU(std::shared_ptr<ImageXYZC> image, bool do_cache)
{
  auto cached = sGpuImageCache.find(image);
  if (cached != sGpuImageCache.end()) {
    return cached->second;
  }

  ImageGpu* cimg = new ImageGpu;
  cimg->allocGpuInterleaved(image.get());
  std::shared_ptr<ImageGpu> shared(cimg);

  if (do_cache) {
    sGpuImageCache[image] = shared;
  }

  return shared;
}

void
renderlib::imageDeallocGPU(std::shared_ptr<ImageXYZC> image)
{
  auto cached = sGpuImageCache.find(image);
  if (cached != sGpuImageCache.end()) {
    // cached->second is a ImageGpu.
    // outstanding shared refs to cached->second will be deallocated!?!?!?!
    cached->second->deallocGpu();
    sGpuImageCache.erase(image);
  }
}

HeadlessGLContext::HeadlessGLContext()
{
#if HAS_EGL
  EGLint lastError = EGL_SUCCESS;

  // Bind the API
  EGLBoolean bindapi_ok = eglBindAPI(EGL_OPENGL_API);
  if (bindapi_ok == EGL_FALSE) {
    LOG_ERROR << "renderlib::initialize, eglBindAPI failed";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  // Select an appropriate configuration
  EGLint numConfigs;
  EGLConfig eglCfg;

  static const EGLint configAttribs[] = { EGL_SURFACE_TYPE,
                                          EGL_PBUFFER_BIT,
                                          EGL_ALPHA_SIZE,
                                          8,
                                          EGL_BLUE_SIZE,
                                          8,
                                          EGL_GREEN_SIZE,
                                          8,
                                          EGL_RED_SIZE,
                                          8,
                                          EGL_DEPTH_SIZE,
                                          AICS_DEFAULT_DEPTH_BUFFER_BITS,
                                          EGL_STENCIL_SIZE,
                                          AICS_DEFAULT_STENCIL_BUFFER_BITS,
                                          EGL_RENDERABLE_TYPE,
                                          EGL_OPENGL_BIT,
                                          EGL_NONE };
  EGLBoolean chooseConfig_ok = eglChooseConfig(eglDpy, configAttribs, &eglCfg, 1, &numConfigs);
  if (chooseConfig_ok == EGL_FALSE) {
    LOG_ERROR << "renderlib::initialize, eglChooseConfig failed";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  // Create a context and make it current
  static const EGLint contextAttribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, AICS_GL_VERSION.major, EGL_CONTEXT_MINOR_VERSION, AICS_GL_VERSION.minor, EGL_NONE
  };
  EGLContext eglCtx = eglCreateContext(eglDpy, eglCfg, EGL_NO_CONTEXT, contextAttribs);
  if (eglCtx == EGL_NO_CONTEXT) {
    LOG_ERROR << "renderlib::initialize, eglCreateContext failed";
  } else {
    LOG_INFO << "created a egl context";
  }
  if ((lastError = eglGetError()) != EGL_SUCCESS) {
    LOG_ERROR << "eglGetError " << lastError;
  }

  m_eglCtx = eglCtx;
#endif
}

HeadlessGLContext::~HeadlessGLContext()
{
#if HAS_EGL
  eglDestroyContext(eglDpy, m_eglCtx);
#endif
}

void
HeadlessGLContext::makeCurrent()
{
#if HAS_EGL
  eglMakeCurrent(eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglCtx);
#endif
}

void
HeadlessGLContext::doneCurrent()
{
#if HAS_EGL
  eglMakeCurrent(eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif
}

RendererGLContext::RendererGLContext()
  : m_ownGLContext(true)
  , m_glContext(nullptr)
  , m_eglContext(nullptr)
  , m_surface(nullptr)
{
}

RendererGLContext::~RendererGLContext() {}

void
RendererGLContext::destroy()
{
  if (m_ownGLContext) {
    delete m_glContext;
    delete m_eglContext;
  } else {
    if (m_glContext)
      m_glContext->moveToThread(QGuiApplication::instance()->thread());
  }

  // schedule this to be deleted only after we're done cleaning up
  if (m_surface)
    m_surface->deleteLater();
}

// to be run from main thread prior to starting render thread
void
RendererGLContext::configure(QOpenGLContext* glContext)
{
  // TODO what do we do when running on Linux desktop??
  // need a "don't bother with EGL switch"?
  if (renderLibHeadless && HAS_EGL) {
  } else {
    if (glContext) {
      m_glContext = glContext;
      m_ownGLContext = false;
    }
  }
}

void
RendererGLContext::initQOpenGLContext()
{
  if (m_ownGLContext) {
    this->m_glContext = renderlib::createOpenGLContext();
  }

  this->m_surface = new QOffscreenSurface();
  this->m_surface->setFormat(this->m_glContext->format());
  this->m_surface->create();

  this->m_glContext->makeCurrent(m_surface);
}

// to be run from render thread
// context is current when returning from this function.
// scenarios:
// headless linux (server mode): always use EGL
// gui linux: always use QOpenGLContext
// else: use QOpenGLContext
void
RendererGLContext::init()
{
  if (renderLibHeadless && HAS_EGL) {
    this->m_eglContext = new HeadlessGLContext();
    this->m_eglContext->makeCurrent();
  } else {
    initQOpenGLContext();
  }
}

void
RendererGLContext::makeCurrent()
{
  if (renderLibHeadless && HAS_EGL) {
    this->m_eglContext->makeCurrent();
  } else {
    this->m_glContext->makeCurrent(this->m_surface);
  }
}

void
RendererGLContext::doneCurrent()
{
  if (m_glContext)
    m_glContext->doneCurrent();
  if (m_eglContext)
    this->m_eglContext->doneCurrent();
}

IRenderWindow*
renderlib::createRenderer(renderlib::RendererType rendererType, RenderSettings* rs)
{
  switch (rendererType) {
    case renderlib::RendererType::RendererType_Raymarch:
      return new RenderGL(rs);
    case renderlib::RendererType::RendererType_Pathtrace:
    default:
      return new RenderGLPT(rs);
  }
}

renderlib::RendererType
renderlib::stringToRendererType(std::string rendererTypeString)
{
  if (rendererTypeString == RenderGL::TYPE_NAME) {
    return RendererType::RendererType_Raymarch;
  } else if (rendererTypeString == RenderGLPT::TYPE_NAME) {
    return RendererType::RendererType_Pathtrace;
  } else {
    return RendererType::RendererType_Pathtrace;
  }
}
std::string
renderlib::rendererTypeToString(renderlib::RendererType rendererType)
{
  switch (rendererType) {
    case renderlib::RendererType_Raymarch:
      return RenderGL::TYPE_NAME;
    case renderlib::RendererType_Pathtrace:
    default:
      return RenderGLPT::TYPE_NAME;
  }
}
