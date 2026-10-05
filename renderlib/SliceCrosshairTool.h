#pragma once

#include "Manipulator.h"

// Interactive crosshairs for the three-pane slice view. Input positions and
// hit thresholds are in framebuffer pixels, matching Gesture::Input. A GUI
// preserving an eight-logical-pixel target should configure 8 * its DPR.
struct SliceCrosshairTool : public ManipulationTool
{
  static constexpr float kDefaultHitThresholdPixels = 8.0f;
  static constexpr float kDefaultPaneGapPixels = 2.0f;

  explicit SliceCrosshairTool(float hitThresholdPixels = kDefaultHitThresholdPixels);

  void action(SceneView& scene, Gesture& gesture) final;
  void draw(SceneView& scene, Gesture& gesture) final;

  // The tool is meaningful only while the triple-slice view has a volume and
  // render settings to operate on.
  bool isActive(const SceneView& scene) const;

  // Return the same layout used for drawing and interaction. The returned
  // rectangles use top-left-origin window coordinates, like Gesture::Input.
  TripleSliceLayout tripleSliceLayout(const SceneView& scene) const;

  // Read-only hit query suitable for choosing a platform cursor. While a drag
  // is captured, this returns the captured direction even outside the pane.
  // A negative threshold uses hitThresholdPixels().
  SliceCrosshairHit hoverHit(const SceneView& scene,
                             const glm::vec2& position,
                             float thresholdPixels = -1.0f,
                             bool useCapturedDrag = true) const;

  void setHitThresholdPixels(float thresholdPixels);
  float hitThresholdPixels() const { return m_hitThresholdPixels; }

  void setPaneGapPixels(float paneGapPixels);
  float paneGapPixels() const { return m_paneGapPixels; }

  bool isDragging() const { return m_dragPane != SlicePane::None; }
  void cancelInteraction();

private:
  bool updateIndicesAt(SceneView& scene,
                       const TripleSliceLayout& layout,
                       SlicePane pane,
                       SliceCrosshairHit hit,
                       const glm::vec2& position);

  float m_hitThresholdPixels;
  float m_paneGapPixels = kDefaultPaneGapPixels;
  SlicePane m_dragPane = SlicePane::None;
  SliceCrosshairHit m_dragHit = SliceCrosshairHit::None;
};
