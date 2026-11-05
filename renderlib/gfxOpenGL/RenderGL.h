#pragma once
#include "AppScene.h"
#include "Status.h"
#include "Timing.h"
#include "gfxapi/IRenderWindow.h"

#include <chrono>
#include <memory>
#include <string>

class BoundingBoxDrawable;
class Image3D;
class ImageXYZC;
class RenderSettings;

class RenderGL : public gfxApi::IRenderWindow
{
public:
  static const std::string TYPE_NAME;
  RenderGL(RenderSettings* rs);
  ~RenderGL() override;

  void initialize(uint32_t w, uint32_t h) override;
  void render(const CCamera& camera) override;
  void renderTo(const CCamera& camera, GLFramebufferObject* fbo) override;
  void resize(uint32_t w, uint32_t h) override;
  void getSize(uint32_t& w, uint32_t& h) override
  {
    w = m_w;
    h = m_h;
  }
  void cleanUpResources() override;

  std::shared_ptr<CStatus> getStatusInterface() override { return m_status; }
  RenderSettings& renderSettings() override;
  Scene* scene() override;
  void setScene(Scene* s) override;

  Image3D* getImage() const { return m_image3d; };

private:
  Image3D* m_image3d;
  RenderSettings* m_renderSettings;

  Scene* m_scene;

  std::shared_ptr<CStatus> m_status;
  Timing m_timingRender;
  std::chrono::time_point<std::chrono::high_resolution_clock> mStartTime;

  int m_w, m_h;

  void initFromScene();
  bool prepareToRender();
  void drawSceneObjects(const CCamera& camera);
};
