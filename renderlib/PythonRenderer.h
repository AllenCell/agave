#pragma once

#include "command.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

class CCamera;
class ImageXYZC;
class RenderSettings;
class Scene;
struct VolumeDimensions;

namespace gfxApi {
class Framebuffer;
class IGestureRenderer;
class IRenderWindow;
}

using PythonRendererArgument = std::variant<int32_t, float, std::string, std::vector<float>, std::vector<int32_t>>;
using PythonRendererArguments = std::vector<PythonRendererArgument>;

struct PythonRendererImage
{
  int32_t width;
  int32_t height;
  std::vector<uint8_t> pixels;
};

using PythonRendererResult = std::variant<std::monostate, std::string, PythonRendererImage>;

class PythonRendererArgumentError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class PythonRendererValueError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Purpose-built synchronous renderer for the Python wrapper around renderlib.
class PythonRenderer final : public RendererCommandInterface
{
public:
  // selectedGpu is a zero-based physical device index; gfxApi::kAutoSelectGpu
  // (-1) lets the backend pick the best graphics-capable device.
  PythonRenderer(const std::string& mode, const std::string& assetPath, int selectedGpu);
  ~PythonRenderer();

  PythonRenderer(const PythonRenderer&) = delete;
  PythonRenderer& operator=(const PythonRenderer&) = delete;

  static std::vector<CommandArgType> commandArgumentTypes(int commandId);
  PythonRendererResult execute(int commandId, const PythonRendererArguments& args);
  std::string loadVolume(std::shared_ptr<ImageXYZC> image,
                         const VolumeDimensions& dimensions,
                         const std::string& name);
  void close();

  void setStreamMode(int32_t mode) override;
  void resizeGL(int x, int y) override;

private:
  PythonRendererResult run(Command& command);
  PythonRendererResult run(RequestRedrawCommand& command);
  PythonRendererResult redraw();
  void initialize(const std::string& mode, const std::string& assetPath, int selectedGpu);

  int32_t m_width = 1024;
  int32_t m_height = 1024;
  bool m_closed = false;
  bool m_ownsRuntime = false;

  std::unique_ptr<RenderSettings> m_renderSettings;
  std::unique_ptr<CCamera> m_camera;
  std::unique_ptr<Scene> m_scene;
  std::unique_ptr<gfxApi::IRenderWindow> m_renderer;
  std::unique_ptr<gfxApi::Framebuffer> m_framebuffer;
  std::unique_ptr<gfxApi::IGestureRenderer> m_gestureRenderer;
  ExecutionContext m_executionContext{};
};
