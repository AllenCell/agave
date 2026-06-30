#pragma once

#include "AppScene.h"
#include "Status.h"
#include "gfxapi/IRenderWindow.h"

#include <memory>

class RenderSettings;

namespace gfxvulkan {

class Backend;

class RenderVk : public gfxApi::IRenderWindow
{
public:
  RenderVk(Backend& backend, RenderSettings* renderSettings);
  ~RenderVk() override;

  void initialize(uint32_t w, uint32_t h) override;
  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void getSize(uint32_t& w, uint32_t& h) override;
  void cleanUpResources() override;

  std::shared_ptr<CStatus> getStatusInterface() override { return m_status; }

  RenderSettings& renderSettings() override;
  Scene* scene() override;
  void setScene(Scene* s) override;

private:
  gfxApi::ClearColor backgroundClearColor() const;
  void logUnimplementedOnce();

  Backend& m_backend;
  RenderSettings* m_renderSettings = nullptr;
  Scene* m_scene = nullptr;
  std::shared_ptr<CStatus> m_status;
  uint32_t m_w = 0;
  uint32_t m_h = 0;
  bool m_loggedUnimplemented = false;
};

} // namespace gfxvulkan
