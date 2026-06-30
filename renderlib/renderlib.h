#pragma once

#include <string>

class RenderSettings;

namespace gfxApi {
class Backend;
class IRenderWindow;
struct InitParams;
}

class renderlib
{
public:
  static int initialize(const gfxApi::InitParams& params, bool listDevices = false);
  static bool supportsHeadlessRendering();
  static void cleanup();

  static std::string assetPath();

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
  static gfxApi::IRenderWindow* createRenderer(RendererType rendererType, RenderSettings* rs = nullptr);
  static RendererType stringToRendererType(std::string rendererTypeString);
  static std::string rendererTypeToString(RendererType rendererType);
};
