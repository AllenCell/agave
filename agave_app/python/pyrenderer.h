#ifndef OFFSCREEN_RENDERER_H
#define OFFSCREEN_RENDERER_H
#pragma once

#include "glad/glad.h"

#include "RenderInterface.h"
#include "command.h"
#include "renderlib/gesture/gesture.h"
#include "renderlib/gfxOpenGL/RendererGLContext.h"
#include "renderlib/graphics/IRenderWindow.h"
#include "renderlib/graphics/GestureGraphicsGL.h"
#include "renderlib/graphics/gl/Util.h"
#include "renderlib/renderlib.h"

#include <QList>
#include <QObject>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QThread>

#include <memory>
#include <string>

class commandBuffer;
class CCamera;
class ImageXYZC;
class RenderSettings;
class Scene;

class OffscreenRenderer : public RenderInterface
{
public:
  OffscreenRenderer();
  virtual ~OffscreenRenderer();

  // RenderInterface

  // tell server to identify this session?
  int Session(const std::string&) override;
  // tell server where files might be (appends to existing)
  int AssetPath(const std::string&) override;
  // load a volume
  int LoadOmeTif(const std::string&) override;
  // load a volume
  int LoadVolumeFromFile(const std::string&, int, int) override;
  // change load same volume file, different time index
  int SetTime(int) override;
  // set camera pos
  int Eye(float, float, float) override;
  // set camera target pt
  int Target(float, float, float) override;
  // set camera up direction
  int Up(float, float, float) override;
  int Aperture(float) override;
  // perspective(0)/ortho(1), fov(degrees)/orthoscale(world units)
  int CameraProjection(int32_t, float) override;
  int Focaldist(float) override;
  int Exposure(float) override;
  int MatDiffuse(int32_t, float, float, float, float) override;
  int MatSpecular(int32_t, float, float, float, float) override;
  int MatEmissive(int32_t, float, float, float, float) override;
  // set num render iterations
  int RenderIterations(int32_t) override;
  // (continuous or on-demand frames)
  int StreamMode(int32_t) override;
  // request new image
  int Redraw() override;
  int SetResolution(int32_t, int32_t) override;
  int Density(float) override;
  // move camera to bound and look at the scene contents
  int FrameScene() override;
  int MatGlossiness(int32_t, float) override;
  // channel index, 1/0 for enable/disable
  int EnableChannel(int32_t, int32_t) override;
  // channel index, window, level.  (Do I ever set these independently?)
  int SetWindowLevel(int32_t, float, float) override;
  // theta, phi in degrees
  int OrbitCamera(float, float) override;
  int TrackballCamera(float, float) override;
  int SkylightTopColor(float, float, float) override;
  int SkylightMiddleColor(float, float, float) override;
  int SkylightBottomColor(float, float, float) override;
  // r, theta, phi
  int LightPos(int32_t, float, float, float) override;
  int LightColor(int32_t, float, float, float) override;
  // x by y size
  int LightSize(int32_t, float, float) override;
  // xmin, xmax, ymin, ymax, zmin, zmax
  int SetClipRegion(float, float, float, float, float, float) override;
  // x, y, z pixel scaling
  int SetVoxelScale(float, float, float) override;
  // channel, method
  int AutoThreshold(int32_t, int32_t) override;
  // channel index, pct_low, pct_high.  (Do I ever set these independently?)
  int SetPercentileThreshold(int32_t, float, float) override;
  int MatOpacity(int32_t, float) override;
  int SetPrimaryRayStepSize(float) override;
  int SetSecondaryRayStepSize(float) override;
  int BackgroundColor(float, float, float) override;
  int SetIsovalueThreshold(int32_t, float, float) override;
  int SetControlPoints(int32_t, std::vector<float>) override;
  int SetBoundingBoxColor(float, float, float) override;
  int ShowBoundingBox(int32_t) override;
  int ShowScaleBar(int32_t) override;
  int ShowTimeStamp(int32_t) override;
  int SetFlipAxis(int32_t, int32_t, int32_t) override;
  int SetInterpolation(int32_t) override;

protected:
  void init();

  void resizeGL(int internalWidth, int internalHeight);

  QImage render();

  void reset(int from = 0);

  void shutDown();

private:
  // GL context for this offscreen renderer (EGL headless or Qt offscreen,
  // chosen by the backend at runtime).
  gfxopengl::RendererGLContext m_rglContext;

  GLFramebufferObject* m_fbo;

  int32_t m_width, m_height;

  // TODO move this info.  This class only knows about some abstract renderer and a scene object.
  void myVolumeInit();
  struct myVolumeData
  {
    RenderSettings* m_renderSettings;
    IRenderWindow* m_renderer;
    Scene* m_scene;
    CCamera* m_camera;
    Gesture m_gesture;
    GestureRendererGL m_gestureRenderer;

    myVolumeData()
      : m_camera(nullptr)
      , m_scene(nullptr)
      , m_renderSettings(nullptr)
      , m_renderer(nullptr)
    {
    }
  } m_myVolumeData;

  ExecutionContext m_ec;

  QImage m_lastRenderedImage;
  std::string m_session;
};

#endif // OFFSCREEN_RENDERER_H
