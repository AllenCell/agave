#include "RenderToFramebuffer.h"

#include "AppScene.h"
#include "Logging.h"

#include <chrono>

namespace {
using Clock = std::chrono::high_resolution_clock;
double
msSince(const Clock::time_point& t)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
}

namespace gfxApi {

void
renderToFramebuffer(Framebuffer& framebuffer,
                    IRenderWindow& renderer,
                    IGestureRenderer& gestureRenderer,
                    SceneView& sceneView,
                    Gesture::Graphics& graphics,
                    float backgroundAlpha)
{
  ClearColor clearColor;
  if (sceneView.scene) {
    clearColor = { sceneView.scene->m_material.m_backgroundColor[0],
                   sceneView.scene->m_material.m_backgroundColor[1],
                   sceneView.scene->m_material.m_backgroundColor[2],
                   backgroundAlpha };
  }

  gestureRenderer.setTargetFramebuffer(&framebuffer);

  auto t0 = Clock::now();
  framebuffer.bind();
  framebuffer.clear(clearColor);
  const double clearMs = msSince(t0);
  t0 = Clock::now();
  gestureRenderer.drawUnderlay(sceneView, graphics);
  framebuffer.release();
  const double underlayMs = msSince(t0);

  t0 = Clock::now();
  renderer.renderTo(sceneView.camera, &framebuffer);
  const double renderMs = msSince(t0);

  t0 = Clock::now();
  framebuffer.bind();
  gestureRenderer.draw(sceneView, graphics);
  framebuffer.release();
  // const double overlayMs = msSince(t0);
  // LOG_INFO << "PROFILE renderToFramebuffer clear=" << clearMs << " underlay=" << underlayMs << " render=" << renderMs
  //         << " overlay=" << overlayMs;
}

} // namespace gfxApi
