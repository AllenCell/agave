#include "renderlib.h"

#include "ImageXYZC.h"
#include "gfxOpenGL/ImageXyzcGpu.h"
#include "Logging.h"
#include "gfxOpenGL/Backend.h"
#include "gfxapi/Backend.h"

#include <QGuiApplication>

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

namespace {

constexpr const char* kRaymarchRendererTypeName = "raymarch";
constexpr const char* kPathtraceRendererTypeName = "pathtrace";

gfxApi::RenderWindowKind
toRenderWindowKind(renderlib::RendererType rendererType)
{
  switch (rendererType) {
    case renderlib::RendererType::RendererType_Raymarch:
      return gfxApi::RenderWindowKind::RaymarchBlended;
    case renderlib::RendererType::RendererType_Pathtrace:
    default:
      return gfxApi::RenderWindowKind::PathTrace;
  }
}

} // namespace

// Backend selection lives here, in renderlib, rather than in gfxapi: the
// abstract gfxapi layer must not depend on any concrete backend. This is the
// one place that maps a BackendKind onto a concrete implementation.
static std::unique_ptr<gfxApi::Backend>
createGraphicsBackend(gfxApi::BackendKind kind, const gfxApi::InitParams& params)
{
  switch (kind) {
    case gfxApi::BackendKind::OpenGL: {
      auto backend = std::make_unique<gfxopengl::Backend>(params);
      if (!backend->isValid()) {
        LOG_ERROR << "createGraphicsBackend: OpenGL backend initialization failed";
        return nullptr;
      }
      LOG_INFO << "createGraphicsBackend: OpenGL backend initialized successfully";
      return backend;
    }
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

std::map<std::shared_ptr<ImageXYZC>, std::shared_ptr<ImageGpu>> renderlib::sGpuImageCache;

static const struct
{
  int major = 4;
  int minor = 1;
} AICS_GL_VERSION;

static const uint32_t AICS_DEFAULT_STENCIL_BUFFER_BITS = 8;

static const uint32_t AICS_DEFAULT_DEPTH_BUFFER_BITS = 24;

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

int
renderlib::initialize(const gfxApi::InitParams& initParams, bool listDevices)
{
  if (renderLibInitialized) {
    return 1;
  }
  gfxApi::InitParams params = initParams;
  renderLibInitialized = true;
  s_assetPath = params.assetPath;

  if (params.headless && !gfxopengl::Backend::supportsHeadless()) {
    params.headless = false;
  }

  LOG_INFO << "Renderlib startup";

  if (params.headless && listDevices) {
    gfxopengl::Backend::listDevices(params.selectedGpu);
    return 0;
  }

  s_graphicsBackend = createGraphicsBackend(gfxApi::BackendKind::OpenGL, params);
  if (!s_graphicsBackend) {
    LOG_ERROR << "renderlib::initialize: failed to create the graphics backend";
    return 0;
  }

  // Transitional bridge for render workers that still use renderlib's legacy
  // context wrappers. The next application-consumer layer removes this state.
  renderLibHeadless = params.headless;
#if HAS_EGL
  eglDpy = static_cast<EGLDisplay>(static_cast<gfxopengl::Backend*>(s_graphicsBackend.get())->eglDisplay());
#endif

  return 1;
}

bool
renderlib::supportsHeadlessRendering()
{
  return gfxopengl::Backend::supportsHeadless();
}

std::string
renderlib::assetPath()
{
  return s_assetPath;
}

gfxApi::Backend*
renderlib::graphicsBackend()
{
  return s_graphicsBackend.get();
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

  s_graphicsBackend.reset();
  LOG_INFO << "graphicsBackend teardown successful";

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

gfxApi::IRenderWindow*
renderlib::createRenderer(renderlib::RendererType rendererType, RenderSettings* rs)
{
  if (!s_graphicsBackend) {
    LOG_ERROR << "renderlib::createRenderer: graphics backend is not initialized";
    return nullptr;
  }

  return s_graphicsBackend->createRenderWindow(toRenderWindowKind(rendererType), rs).release();
}

renderlib::RendererType
renderlib::stringToRendererType(std::string rendererTypeString)
{
  if (rendererTypeString == kRaymarchRendererTypeName) {
    return RendererType::RendererType_Raymarch;
  } else if (rendererTypeString == kPathtraceRendererTypeName) {
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
      return kRaymarchRendererTypeName;
    case renderlib::RendererType_Pathtrace:
    default:
      return kPathtraceRendererTypeName;
  }
}
