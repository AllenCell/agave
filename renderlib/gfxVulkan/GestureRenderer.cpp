#include "GestureRenderer.h"

namespace gfxvulkan {

bool
GestureRenderer::selectionBufferMatches(int width, int height) const
{
  return width == m_selectionWidth && height == m_selectionHeight;
}

bool
GestureRenderer::updateSelectionBuffer(int width, int height)
{
  m_selectionWidth = width;
  m_selectionHeight = height;
  return true;
}

void
GestureRenderer::clearSelectionBuffer()
{
  m_selectionWidth = 0;
  m_selectionHeight = 0;
}

bool
GestureRenderer::pick(const Gesture::Input& input, const SceneView::Viewport& viewport, uint32_t& selectionCode)
{
  (void)input;
  (void)viewport;
  selectionCode = Gesture::Graphics::k_noSelectionCode;
  return false;
}

void
GestureRenderer::drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics)
{
  (void)sceneView;
  graphics.clearCommands();
}

void
GestureRenderer::draw(SceneView& sceneView, Gesture::Graphics& graphics)
{
  (void)sceneView;
  graphics.clearCommands();
}

} // namespace gfxvulkan
