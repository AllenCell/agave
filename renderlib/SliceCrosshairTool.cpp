#include "SliceCrosshairTool.h"

#include "AppScene.h"
#include "Enumerations.h"
#include "ImageXYZC.h"

#include <algorithm>

namespace {
constexpr float CROSSHAIR_LINE_THICKNESS = 1.5f;
constexpr float CROSSHAIR_SHADOW_OFFSET = 1.0f;
const glm::vec3 CROSSHAIR_COLOR(1.0f);
const glm::vec3 CROSSHAIR_SHADOW_COLOR(0.2f);

glm::vec2
currentPointerPosition(const Gesture::Input::Button& button)
{
  return button.pressedPosition + button.drag;
}
}

SliceCrosshairTool::SliceCrosshairTool(float hitThresholdPixels)
  : ManipulationTool(0)
  , m_hitThresholdPixels(std::max(0.0f, hitThresholdPixels))
{
}

bool
SliceCrosshairTool::isActive(const SceneView& scene) const
{
  return scene.renderSettings && scene.renderSettings->m_SliceView.mode == SliceViewMode::Triple && scene.scene &&
         scene.scene->m_volume;
}

TripleSliceLayout
SliceCrosshairTool::tripleSliceLayout(const SceneView& scene) const
{
  if (!isActive(scene)) {
    return {};
  }

  const glm::ivec2 viewportSize = scene.viewport.region.size();
  if (viewportSize.x <= 0 || viewportSize.y <= 0) {
    return {};
  }

  const glm::vec3 physicalDimensions = glm::max(scene.scene->m_volume->getPhysicalDimensions(), glm::vec3(0.000001f));
  return computeTripleSliceLayout(physicalDimensions, viewportSize, m_paneGapPixels);
}

SliceCrosshairHit
SliceCrosshairTool::hoverHit(const SceneView& scene,
                             const glm::vec2& position,
                             float thresholdPixels,
                             bool useCapturedDrag) const
{
  if (!isActive(scene)) {
    return SliceCrosshairHit::None;
  }
  if (useCapturedDrag && isDragging()) {
    return m_dragHit;
  }

  const TripleSliceLayout layout = tripleSliceLayout(scene);
  const SlicePane pane = slicePaneAt(layout, position);
  const float threshold = thresholdPixels < 0.0f ? m_hitThresholdPixels : std::max(0.0f, thresholdPixels);
  return hitTestSliceCrosshair(scene.renderSettings->m_SliceView, pane, layout, position, threshold);
}

void
SliceCrosshairTool::setHitThresholdPixels(float thresholdPixels)
{
  m_hitThresholdPixels = std::max(0.0f, thresholdPixels);
}

void
SliceCrosshairTool::setPaneGapPixels(float paneGapPixels)
{
  m_paneGapPixels = std::max(0.0f, paneGapPixels);
}

void
SliceCrosshairTool::cancelInteraction()
{
  m_dragPane = SlicePane::None;
  m_dragHit = SliceCrosshairHit::None;
}

bool
SliceCrosshairTool::updateIndicesAt(SceneView& scene,
                                    const TripleSliceLayout& layout,
                                    SlicePane pane,
                                    SliceCrosshairHit hit,
                                    const glm::vec2& position)
{
  if (!scene.renderSettings || pane == SlicePane::None || hit == SliceCrosshairHit::None) {
    return false;
  }

  SliceViewState& state = scene.renderSettings->m_SliceView;
  const bool changed = updateSliceIndicesFromPane(state, pane, slicePaneUv(layout.pane(pane), position), hit);
  if (changed) {
    scene.renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
  }
  return changed;
}

void
SliceCrosshairTool::action(SceneView& scene, Gesture& gesture)
{
  Gesture::Input::Button& button = gesture.input.mbs[Gesture::Input::kButtonLeft];
  if (!isActive(scene)) {
    cancelInteraction();
    return;
  }

  const TripleSliceLayout layout = tripleSliceLayout(scene);

  // Some window systems report a distinct double-click followed immediately
  // by its release, so the latched event is intentionally independent of the
  // button action observed in this update.
  if (gesture.input.consumeDoubleClick(Gesture::Input::kButtonLeft)) {
    cancelInteraction();
    const glm::vec2 position = button.pressedPosition;
    const SlicePane pane = slicePaneAt(layout, position);
    updateIndicesAt(scene, layout, pane, SliceCrosshairHit::Both, position);
    gesture.input.reset(Gesture::Input::kButtonLeft);
    return;
  }

  // Input adapters may deliver press, move, and release events between two
  // render updates. Capture from the original press position for any still
  // pending left-button action so a coalesced drag is not lost.
  if (!isDragging() && button.action != Gesture::Input::Action::kNone) {
    cancelInteraction();

    const glm::vec2 position = button.pressedPosition;
    const SlicePane pane = slicePaneAt(layout, position);

    const SliceCrosshairHit hit =
      hitTestSliceCrosshair(scene.renderSettings->m_SliceView, pane, layout, position, m_hitThresholdPixels);
    if (hit == SliceCrosshairHit::None) {
      return;
    }

    m_dragPane = pane;
    m_dragHit = hit;
    updateIndicesAt(scene, layout, m_dragPane, m_dragHit, position);
    if (button.action == Gesture::Input::Action::kPress) {
      return;
    }
  }

  if (button.action == Gesture::Input::Action::kDrag && isDragging()) {
    updateIndicesAt(scene, layout, m_dragPane, m_dragHit, currentPointerPosition(button));
    return;
  }

  if (button.action == Gesture::Input::Action::kRelease && isDragging()) {
    updateIndicesAt(scene, layout, m_dragPane, m_dragHit, currentPointerPosition(button));
    cancelInteraction();
    gesture.input.reset(Gesture::Input::kButtonLeft);
  }
}

void
SliceCrosshairTool::draw(SceneView& scene, Gesture& gesture)
{
  if (!isActive(scene)) {
    return;
  }

  const TripleSliceLayout layout = tripleSliceLayout(scene);
  const float viewportHeight = static_cast<float>(scene.viewport.region.size().y);
  const uint32_t selectionCode = Gesture::Graphics::k_noSelectionCode;

  auto addScreenLine = [&](const glm::vec2& p0, const glm::vec2& p1, const glm::vec3& color) {
    gesture.graphics.addLineStrip({ Gesture::Graphics::VertsCode(glm::vec3(p0, 1.0f), color, 1.0f, selectionCode),
                                    Gesture::Graphics::VertsCode(glm::vec3(p1, 1.0f), color, 1.0f, selectionCode) },
                                  CROSSHAIR_LINE_THICKNESS,
                                  false,
                                  Gesture::Graphics::CommandSequence::k2dScreen);
  };

  auto drawPaneCrosshair = [&](SlicePane pane) {
    const SliceRect& rect = layout.pane(pane);
    if (!rect.isValid()) {
      return;
    }

    const glm::vec2 uv = sliceCrosshairUv(scene.renderSettings->m_SliceView, pane);
    const float left = rect.x;
    const float right = rect.x + rect.width;
    const float bottom = viewportHeight - (rect.y + rect.height);
    const float top = viewportHeight - rect.y;
    const float x = left + uv.x * rect.width;
    const float y = bottom + uv.y * rect.height;

    // Match the slice shader's one-pixel lower/right shadow, then draw the
    // white crosshair over it. Both carry the same simple selection code.
    addScreenLine(glm::vec2(x + CROSSHAIR_SHADOW_OFFSET, bottom),
                  glm::vec2(x + CROSSHAIR_SHADOW_OFFSET, top),
                  CROSSHAIR_SHADOW_COLOR);
    addScreenLine(glm::vec2(left, y - CROSSHAIR_SHADOW_OFFSET),
                  glm::vec2(right, y - CROSSHAIR_SHADOW_OFFSET),
                  CROSSHAIR_SHADOW_COLOR);
    addScreenLine(glm::vec2(x, bottom), glm::vec2(x, top), CROSSHAIR_COLOR);
    addScreenLine(glm::vec2(left, y), glm::vec2(right, y), CROSSHAIR_COLOR);
  };

  drawPaneCrosshair(SlicePane::XY);
  drawPaneCrosshair(SlicePane::YZ);
  drawPaneCrosshair(SlicePane::XZ);
}
