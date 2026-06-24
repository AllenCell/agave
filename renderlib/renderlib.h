#pragma once

#include "glad/glad.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <map>
#include <memory>
#include <string>

struct ImageGpu;
class ImageXYZC;
class IRenderWindow;
class RenderSettings;

namespace gfxApi {
class Backend;
}

class renderlib
{
public:
  static int initialize(std::string assetPath, bool headless = false, bool listDevices = false, int selectedGpu = 0);
  static void clearGpuVolumeCache();
  static void cleanup();

  static std::string assetPath();

  // usage of this cache:
  // websocketserver:
  //   preloaded images will be cached at preload time and never deallocated
  //   images loaded by command will not be cached here.
  //   gpu cache will stay alive until shutdown
  // desktop gui:
  //   there is no preloading.
  //   gpu cache will be cleared every time a new volume is loaded.
  // if do_cache is false, caller is responsible for deallocating from gpu mem.
  // if do_cache is true, caller can be assured gpu mem will stay cached until an event that clears the cache
  // events that clear the cache:
  // websocketserver:
  //    shutdown
  // desktop:
  //    load new image or shutdown
  static std::shared_ptr<ImageGpu> imageAllocGPU(std::shared_ptr<ImageXYZC> image, bool do_cache = true);
  static void imageDeallocGPU(std::shared_ptr<ImageXYZC> image);

  // The active graphics backend (created during initialize). Null before
  // initialize / after cleanup. Callers needing backend-specific facilities can
  // downcast based on kind().
  static gfxApi::Backend* graphicsBackend();

  enum RendererType
  {
    RendererType_Pathtrace,
    RendererType_Raymarch
  };
  // factory method for creating renderers
  static IRenderWindow* createRenderer(RendererType rendererType, RenderSettings* rs = nullptr);
  static RendererType stringToRendererType(std::string rendererTypeString);
  static std::string rendererTypeToString(RendererType rendererType);

private:
  static std::map<std::shared_ptr<ImageXYZC>, std::shared_ptr<ImageGpu>> sGpuImageCache;
};
