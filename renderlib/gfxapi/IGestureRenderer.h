#pragma once

#include "SceneView.h"
#include "gesture/gesture.h"

#include <cstdint>

namespace gfxApi {

class Framebuffer;

class IGestureRenderer
{
public:
  virtual ~IGestureRenderer() = default;

  // Backends that render to an explicit target (e.g. Vulkan) are told which
  // framebuffer to composite the gesture overlay onto before draw() is called.
  // Backends that rely on a bound/current framebuffer (OpenGL) ignore this.
  virtual void setTargetFramebuffer(Framebuffer* target) { (void)target; }

  virtual bool selectionBufferMatches(int width, int height) const = 0;
  virtual bool updateSelectionBuffer(int width, int height) = 0;
  virtual void clearSelectionBuffer() = 0;

  virtual bool pick(const Gesture::Input& input,
                    const SceneView::Viewport& viewport,
                    uint32_t& selectionCode) = 0;

  virtual void drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics) = 0;
  virtual void draw(SceneView& sceneView, Gesture::Graphics& graphics) = 0;
};

} // namespace gfxApi
