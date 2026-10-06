#include "GestureRenderer.h"

namespace gfxopengl {

bool
GestureRenderer::selectionBufferMatches(int width, int height) const
{
  return m_selection.resolution == glm::ivec2(width, height);
}

bool
GestureRenderer::updateSelectionBuffer(int width, int height)
{
  return m_selection.update(glm::ivec2(width, height));
}

void
GestureRenderer::clearSelectionBuffer()
{
  m_selection.clear();
}

bool
GestureRenderer::pick(const Gesture::Input& input, const SceneView::Viewport& viewport, uint32_t& selectionCode)
{
  return m_renderer.pick(m_selection, input, viewport, selectionCode);
}

void
GestureRenderer::drawUnderlay(SceneView& sceneView, Gesture::Graphics& graphics)
{
  m_renderer.drawUnderlay(sceneView, &m_selection, graphics);
}

void
GestureRenderer::draw(SceneView& sceneView, Gesture::Graphics& graphics)
{
  m_renderer.draw(sceneView, &m_selection, graphics);
}

} // namespace gfxopengl
