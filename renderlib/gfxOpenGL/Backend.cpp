#include "Backend.h"

#include "GestureRenderer.h"
#include "GLContext.h"
#include "GLFramebufferObject.h"
#include "HeadlessGLContext.h"
#include "Logging.h"
#include "RenderGL.h"
#include "RenderGLPT.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLDebugLogger>

// EGL is only available on Linux in this project.
#if defined(__APPLE__) || defined(_WIN32)
#define GFXOPENGL_HAS_EGL 0
#else
#define GFXOPENGL_HAS_EGL 1
#endif

#if GFXOPENGL_HAS_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#include <string>

namespace gfxopengl {

namespace {

GLenum
toGlInternalFormat(gfxApi::FramebufferColorFormat format)
{
  switch (format) {
    case gfxApi::FramebufferColorFormat::Rgba8:
      return GL_RGBA8;
    case gfxApi::FramebufferColorFormat::Rgba32F:
      return GL_RGBA32F;
  }
  return GL_RGBA8;
}

} // namespace

std::unique_ptr<gfxApi::IGestureRenderer>
Backend::createGestureRenderer()
{
  return std::make_unique<GestureRenderer>();
}

std::unique_ptr<gfxApi::IRenderWindow>
Backend::createRenderWindow(gfxApi::RenderWindowKind kind, RenderSettings* renderSettings)
{
  switch (kind) {
    case gfxApi::RenderWindowKind::RaymarchBlended:
      return std::make_unique<RenderGL>(renderSettings);
    case gfxApi::RenderWindowKind::PathTrace:
    default:
      return std::make_unique<RenderGLPT>(renderSettings);
  }
}

std::unique_ptr<gfxApi::Framebuffer>
Backend::createFramebuffer(const gfxApi::FramebufferDesc& desc)
{
  return std::make_unique<GLFramebufferObject>(
    desc.width, desc.height, toGlInternalFormat(desc.colorFormat), desc.depthStencil);
}

#if GFXOPENGL_HAS_EGL
namespace {

void
checkEGLError(const std::string& message)
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
  EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  LOG_INFO << "eglGetDisplay returns " << eglDisplay;
  checkEGLError("Failed eglGetDisplay");
  return eglDisplay;
}

// Enumerate the available EGL devices (logging each one) and return a display
// for the requested GPU, or the default display if selection is unavailable.
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

} // namespace
#endif // GFXOPENGL_HAS_EGL

namespace {
void
logGLMessage(const QOpenGLDebugMessage& message)
{
  LOG_DEBUG << message.message().toStdString();
}
} // namespace

Backend::Backend(const gfxApi::InitParams& params)
  : m_params(params)
{
  bool contextOk = false;
#if GFXOPENGL_HAS_EGL
  if (m_params.headless) {
    contextOk = initEGLContext();
  } else
#endif
  {
    contextOk = initWindowedContext();
  }

  // Only load GL once a bootstrap context is current. m_valid reflects whether
  // the whole bring-up succeeded; callers must discard an invalid backend.
  m_valid = contextOk && initGL();
}

#if GFXOPENGL_HAS_EGL
bool
Backend::initEGLContext()
{
  // 1. Get a display for the selected device
  EGLDisplay dpy = initEGLDisplay(m_params.selectedGpu);
  m_eglDisplay = dpy;
  if (dpy == EGL_NO_DISPLAY) {
    LOG_ERROR << "gfxopengl::Backend: failed to get an EGL display";
    return false;
  }

  // 2. Initialize EGL
  EGLint major, minor;
  if (eglInitialize(dpy, &major, &minor) == EGL_FALSE) {
    LOG_ERROR << "gfxopengl::Backend: eglInitialize failed (eglGetError " << eglGetError() << ")";
    return false;
  }

  // 3. Bind the API
  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
    LOG_ERROR << "gfxopengl::Backend: eglBindAPI failed (eglGetError " << eglGetError() << ")";
    return false;
  }

  // 4. Create a headless GL context and make it current so GL can be loaded.
  m_headlessContext = std::make_unique<HeadlessGLContext>(m_eglDisplay);
  if (!m_headlessContext->isValid()) {
    LOG_ERROR << "gfxopengl::Backend: failed to create headless GL context";
    return false;
  }
  m_headlessContext->makeCurrent();
  return true;
}
#endif // GFXOPENGL_HAS_EGL

bool
Backend::initWindowedContext()
{
  // Windowed / non-headless: create a Qt offscreen bootstrap context and make
  // it current so GL can be loaded. Per-thread render contexts are created
  // separately (see RendererGLContext).
  m_dummyContext = createOpenGLContext();
  if (!m_dummyContext || !m_dummyContext->isValid()) {
    LOG_ERROR << "gfxopengl::Backend: failed to create an OpenGL context";
    return false;
  }

  m_dummySurface = new QOffscreenSurface();
  m_dummySurface->setFormat(m_dummyContext->format());
  m_dummySurface->create();
  if (!m_dummySurface->isValid()) {
    LOG_ERROR << "gfxopengl::Backend: QOffscreenSurface is not valid";
    return false;
  }
  LOG_INFO << "Created offscreen surface";

  if (!m_dummyContext->makeCurrent(m_dummySurface)) {
    LOG_ERROR << "gfxopengl::Backend: failed to makeCurrent on offscreen surface";
    return false;
  }
  LOG_INFO << "Made context current on offscreen surface";
  return true;
}

Backend::~Backend()
{
  delete m_debugLogger;
  m_debugLogger = nullptr;

  // Destroy the headless bootstrap context before terminating its display.
  m_headlessContext.reset();

  // Destroy the windowed bootstrap context / surface.
  delete m_dummySurface;
  m_dummySurface = nullptr;
  delete m_dummyContext;
  m_dummyContext = nullptr;

#if GFXOPENGL_HAS_EGL
  if (m_params.headless && m_eglDisplay) {
    eglTerminate(static_cast<EGLDisplay>(m_eglDisplay));
  }
#endif
}

bool
Backend::initGL()
{
  if (m_params.enableDebug) {
    m_debugLogger = new QOpenGLDebugLogger();
    QObject::connect(m_debugLogger, &QOpenGLDebugLogger::messageLogged, logGLMessage);
    if (m_debugLogger->initialize()) {
      m_debugLogger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
      m_debugLogger->enableMessages();
    }
  }

  // note: there MUST be a valid current gl context in order to run this:
  if (!gladLoadGL()) {
    LOG_ERROR << "gfxopengl::Backend: failed to load GL (gladLoadGL)";
    return false;
  }

  LOG_INFO << "GL_VENDOR: " << std::string((char*)glGetString(GL_VENDOR));
  LOG_INFO << "GL_RENDERER: " << std::string((char*)glGetString(GL_RENDERER));

  return true;
}

void
Backend::listDevices(int selectedGpu)
{
#if GFXOPENGL_HAS_EGL
  // initEGLDisplay logs each available device as a side effect.
  initEGLDisplay(selectedGpu);
#else
  (void)selectedGpu;
  LOG_WARNING << "gfxopengl::Backend::listDevices: EGL device enumeration is not available in this build";
#endif
}

} // namespace gfxopengl
