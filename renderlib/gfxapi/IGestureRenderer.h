#pragma once

#include "SceneView.h"
#include "gesture/gesture.h"

#include <cstdint>

namespace gfxApi {

class IGestureRenderer
{
public:
  virtual ~IGestureRenderer() = default;

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
