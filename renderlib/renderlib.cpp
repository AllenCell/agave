#include "renderlib.h"

#include "Logging.h"
#include "gfxOpenGL/Backend.h"
#include "gfxapi/Backend.h"
#if AGAVE_HAS_VULKAN
#include "gfxVulkan/Backend.h"
#endif

#include <memory>
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

// Owner of the active graphics backend selected at process startup.
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
      // A backend that failed to bring up its GL context is unusable; discard it.
      if (!backend->isValid()) {
        LOG_ERROR << "createGraphicsBackend: OpenGL backend initialization failed";
        return nullptr;
      }
      LOG_INFO << "createGraphicsBackend: OpenGL backend initialized successfully";
      return backend;
    }
    case gfxApi::BackendKind::Vulkan:
#if AGAVE_HAS_VULKAN
    {
      auto backend = std::make_unique<gfxvulkan::Backend>(params);
      if (!backend->isValid()) {
        LOG_ERROR << "createGraphicsBackend: Vulkan backend initialization failed";
        return nullptr;
      }
      LOG_INFO << "createGraphicsBackend: Vulkan backend initialized successfully";
      return backend;
    }
#else
      LOG_ERROR << "createGraphicsBackend: Vulkan backend requested, but this build does not include Vulkan support";
      return nullptr;
#endif
    case gfxApi::BackendKind::WebGPU:
    default:
      LOG_ERROR << "createGraphicsBackend: requested backend kind is not supported in this build";
      return nullptr;
  }
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

  // Headless rendering requires EGL support, which the OpenGL backend only
  // provides on some platforms (not Windows / macOS).
  if (params.backendKind == gfxApi::BackendKind::OpenGL && params.headless && !gfxopengl::Backend::supportsHeadless()) {
    params.headless = false;
  }

  LOG_INFO << "Renderlib startup";

  // --list-devices: enumerate the available GPUs and quit. This only needs the
  // backend's device enumeration, not a fully initialized backend.
  if (listDevices) {
    if (params.backendKind == gfxApi::BackendKind::Vulkan) {
#if AGAVE_HAS_VULKAN
      gfxvulkan::Backend::listDevices(params.selectedGpu);
#else
      LOG_ERROR << "renderlib::initialize: Vulkan device listing requested, but Vulkan support is not built";
#endif
    } else if (params.headless) {
      gfxopengl::Backend::listDevices(params.selectedGpu);
    }
    return 0;
  }

  // Create the graphics backend. Returns null if it fails.
  s_graphicsBackend = createGraphicsBackend(params.backendKind, params);
  if (!s_graphicsBackend) {
    LOG_ERROR << "renderlib::initialize: failed to create the graphics backend";
    return 0;
  }

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
renderlib::cleanup()
{
  if (!renderLibInitialized) {
    return;
  }
  LOG_INFO << "Renderlib shutdown";

  // The OpenGL backend owns all GL contexts (headless / windowed bootstrap),
  // the EGL display, and the debug logger, and tears them down in its destructor.
  s_graphicsBackend.reset();
  LOG_INFO << "graphicsBackend teardown successful";

  renderLibInitialized = false;
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
