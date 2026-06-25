#pragma once
#include "gfxapi/IRenderWindow.h"

#include "AppScene.h"
#include "RenderSettings.h"

#include "gfxOpenGL/ImageXyzcGpu.h"
#include "Status.h"
#include "Timing.h"

#include <memory>
#include <string>

class BoundingBoxDrawable;
class Framebuffer;
class FSQ;
class ImageXYZC;
class Image3D;
class RectImage2D;
class GLCopyShader;
class GLPTVolumeShader;
class GLToneMapShader;

class RenderGLPT : public gfxApi::IRenderWindow
{
public:
  static const std::string TYPE_NAME;

  RenderGLPT(RenderSettings* rs);
  ~RenderGLPT() override;

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
  RenderSettings& renderSettings() override;
  Scene* scene() override;
  void setScene(Scene* s) override;

  std::shared_ptr<CStatus> getStatusInterface() override { return m_status; }

  Image3D* getImage() const { return nullptr; };

  // just draw into my own fbo.
  void doRender(const CCamera& camera);
  // draw my fbo texture into the current render target
  void drawImage();

  size_t getGpuBytes();

private:
  RenderSettings* m_renderSettings;
  Scene* m_scene;

  void initFB(uint32_t w, uint32_t h);
  void initVolumeTextureGpu();
  void cleanUpFB();

  ImageGpu m_imgGpu;

  RectImage2D* m_imagequad;

  // the rgba8 buffer for display
  Framebuffer* m_fb;

  FSQ* m_fsq;

  // the rgbaf32 buffer for rendering
  Framebuffer* m_fbF32;
  GLPTVolumeShader* m_renderBufferShader;

  // the rgbaf32 accumulation buffer that holds the progressively rendered image
  Framebuffer* m_fbF32Accum;
  GLCopyShader* m_copyShader;
  GLToneMapShader* m_toneMapShader;

  // screen size auxiliary buffers for rendering
  unsigned int* m_randomSeeds1;
  unsigned int* m_randomSeeds2;
  // incrementing integer to give to shader
  int m_RandSeed;

  int m_w, m_h;

  Timing m_timingRender, m_timingBlur, m_timingPostProcess, m_timingDenoise;
  std::shared_ptr<CStatus> m_status;

  size_t m_gpuBytes;
};
