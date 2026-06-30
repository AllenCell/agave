#pragma once

#include "gfxapi/IGestureRenderer.h"

namespace gfxvulkan {

class GestureRenderer : public gfxApi::IGestureRenderer
{
public:
  bool selectionBufferMatches(int width, int height) const override;
  bool updateSelectionBuffer(int width, int height) override;
  void clearSelectionBuffer() override;

  bool pick(const Gesture::Input& input, const SceneView::Viewport& viewport, uint32_t& selectionCode) override;

  void drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics) override;
  void draw(SceneView& sceneView, Gesture::Graphics& graphics) override;

private:
  int m_selectionWidth = 0;
  int m_selectionHeight = 0;
};

} // namespace gfxvulkan
