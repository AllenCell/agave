#include "RenderToFramebuffer.h"

#include "AppScene.h"

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

  framebuffer.bind();
  framebuffer.clear(clearColor);
  gestureRenderer.drawUnderlay(sceneView, graphics);
  framebuffer.release();

  renderer.renderTo(sceneView.camera, &framebuffer);

  framebuffer.bind();
  gestureRenderer.draw(sceneView, graphics);
  framebuffer.release();
}

} // namespace gfxApi
