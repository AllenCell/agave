#pragma once

#include "Status.h"
#include "Timing.h"
#include "gfxOpenGL/ImageXyzcGpu.h"
#include "gfxapi/IRenderWindow.h"

#include <chrono>
#include <memory>

class FSQ;
class GLSliceShader;
class RenderSettings;

class RenderGLSlice : public gfxApi::IRenderWindow
{
public:
  explicit RenderGLSlice(RenderSettings* renderSettings);
  ~RenderGLSlice() override;

  void initialize(uint32_t w, uint32_t h) override;
  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void getSize(uint32_t& w, uint32_t& h) override;
  void cleanUpResources() override;
  std::shared_ptr<CStatus> getStatusInterface() override { return m_status; }
  RenderSettings& renderSettings() override;
  Scene* scene() override;
  void setScene(Scene* scene) override;

private:
  bool prepareToRender();
  void initVolumeTexture();
  void updateActiveColormaps();
  void draw(int viewportWidth, int viewportHeight);

  RenderSettings* m_renderSettings = nullptr;
  Scene* m_scene = nullptr;
  ImageGpu m_image;
  FSQ* m_fullscreenQuad = nullptr;
  GLSliceShader* m_shader = nullptr;
  uint32_t m_w = 0;
  uint32_t m_h = 0;
  std::shared_ptr<CStatus> m_status;
  Timing m_timing;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
};
