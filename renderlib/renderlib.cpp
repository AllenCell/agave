#include "renderlib.h"

#include "ImageXYZC.h"
#include "ImageXyzcGpu.h"
#include "Logging.h"
#include "RenderGL.h"
#include "RenderGLPT.h"
#include "gfxOpenGL/Backend.h"
#include "gfxOpenGL/GLContext.h"
#include "gfxapi/Backend.h"

#include <string>

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
    case gfxApi::BackendKind::OpenGL: {
      auto backend = std::make_unique<gfxopengl::Backend>(params);
      // A backend that failed to bring up its GL context is unusable; discard it.
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

std::map<std::shared_ptr<ImageXYZC>, std::shared_ptr<ImageGpu>> renderlib::sGpuImageCache;

int
renderlib::initialize(std::string assetPath, bool headless, bool listDevices, int selectedGpu)
{
  if (renderLibInitialized) {
    return 1;
  }
  renderLibInitialized = true;
  s_assetPath = assetPath;

  // Headless rendering requires EGL support, which the OpenGL backend only
  // provides on some platforms (not Windows / macOS).
  if (headless && !gfxopengl::Backend::supportsHeadless()) {
    headless = false;
  }

  LOG_INFO << "Renderlib startup";

  // --list-devices: enumerate the available GPUs and quit. This only needs the
  // backend's device enumeration, not a fully initialized backend.
  if (headless && listDevices) {
    gfxopengl::Backend::listDevices(selectedGpu);
    return 0;
  }

  // Register AGAVE's GL format as the Qt default (for windowed surfaces).
  QSurfaceFormat::setDefaultFormat(gfxopengl::getSurfaceFormat());

  // Create the graphics backend. Returns null if it fails.
  s_graphicsBackend =
    createGraphicsBackend(gfxApi::BackendKind::OpenGL, gfxApi::InitParams{ assetPath, headless, selectedGpu });
  if (!s_graphicsBackend) {
    LOG_ERROR << "renderlib::initialize: failed to create the graphics backend";
    return 0;
  }

  return 1;
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

  // The OpenGL backend owns all GL contexts (headless / windowed bootstrap),
  // the EGL display, and the debug logger, and tears them down in its destructor.
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
