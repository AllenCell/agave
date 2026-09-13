#pragma once

#include "gfxapi/IGestureRenderer.h"
#include "gfxOpenGL/GestureGraphicsGL.h"

namespace gfxopengl {

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
  SelectionBuffer m_selection;
  GestureRendererGL m_renderer;
};

} // namespace gfxopengl
