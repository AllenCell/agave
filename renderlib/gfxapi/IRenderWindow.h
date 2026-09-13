#pragma once

#include "gfxapi/Framebuffer.h"

#include <inttypes.h>

class CCamera;
class CStatus;
class RenderSettings;
class Scene;

#include <memory>

namespace gfxApi {

class IRenderWindow
{
public:
  IRenderWindow();
  virtual ~IRenderWindow();

  virtual void initialize(uint32_t w, uint32_t h) = 0;
  virtual void render(const CCamera& camera) = 0;
  virtual void renderTo(const CCamera& camera, Framebuffer* fbo) = 0;
  virtual void resize(uint32_t w, uint32_t h) = 0;
  virtual void getSize(uint32_t& w, uint32_t& h) = 0;
  virtual void cleanUpResources() {}

  // An interface for reporting statistics and other data updates
  // The IRenderWindow is the first to create this object but it can get shared with other widgets in the gui.
  // Alternative impl could be to create at app layer and pass down into renderers
  virtual std::shared_ptr<CStatus> getStatusInterface() { return nullptr; }

  // I own these.
  virtual RenderSettings& renderSettings() = 0;

  virtual Scene* scene() = 0;
  virtual void setScene(Scene* s) = 0;
};

} // namespace gfxApi
