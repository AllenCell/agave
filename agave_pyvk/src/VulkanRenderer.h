#pragma once

#include "renderlib/command.h"

#include <nanobind/nanobind.h>

#include <cstdint>
#include <memory>
#include <string>

class CCamera;
class RenderSettings;
class Scene;

namespace gfxApi {
class Framebuffer;
class IGestureRenderer;
class IRenderWindow;
}

class VulkanRenderer final : public RendererCommandInterface
{
public:
  VulkanRenderer(const std::string& mode, const std::string& assetPath, int selectedGpu);
  ~VulkanRenderer();

  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;

  nanobind::object execute(int commandId, const nanobind::args& args);
  void close();

  void setStreamMode(int32_t mode) override;
  void resizeGL(int x, int y) override;

private:
  nanobind::object run(Command& command);
  nanobind::object redraw();
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
