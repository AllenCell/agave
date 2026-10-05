#pragma once

#include <cstdint>

#include <glm/glm.hpp>

// Slice renderer modes. The numeric values are deliberately independent from
// the application's renderer ids (2..5).
enum class SliceViewMode : uint8_t
{
  Z = 0,
  Y,
  X,
  Triple
};

enum class SlicePane : uint8_t
{
  None = 0,
  XY,
  YZ,
  XZ
};

enum class SliceCrosshairHit : uint8_t
{
  None = 0,
  Horizontal,
  Vertical,
  Both
};

struct SliceRect
{
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  bool isValid() const;
  bool contains(const glm::vec2& point) const;
};

// Pane coordinates use a top-left origin so they match Qt mouse coordinates.
// The panes are arranged as XZ above XY, with YZ to the right of XY.
struct TripleSliceLayout
{
  SliceRect xy;
  SliceRect yz;
  SliceRect xz;

  const SliceRect& pane(SlicePane paneId) const;
};

struct SliceViewState
{
  SliceViewMode mode = SliceViewMode::Z;
  glm::ivec3 indices = glm::ivec3(0);
  glm::ivec3 dimensions = glm::ivec3(1);

  void setDimensions(const glm::ivec3& newDimensions, bool centerIndices = false);
  void clampIndices();

  bool isSingleSlice() const;
  int activeAxis() const;
  int activeIndex() const;
  int activeDimension() const;
  void setActiveIndex(int index);
};

TripleSliceLayout computeTripleSliceLayout(
  const glm::vec3& physicalDimensions, const glm::ivec2& viewportSize, float gapPixels = 2.0f);
SlicePane slicePaneAt(const TripleSliceLayout& layout, const glm::vec2& point);

// Returns pane-local UV coordinates, clamped to [0, 1]. U increases to the
// right and V increases upward even though pane positions use a top-left origin.
glm::vec2 slicePaneUv(const SliceRect& pane, const glm::vec2& point);
glm::vec2 sliceCrosshairUv(const SliceViewState& state, SlicePane pane);
SliceCrosshairHit hitTestSliceCrosshair(const SliceViewState& state,
                                        SlicePane pane,
                                        const TripleSliceLayout& layout,
                                        const glm::vec2& point,
                                        float thresholdPixels);

// Apply pane-local UV coordinates to the axes represented by a pane. A
// vertical crosshair changes U, a horizontal crosshair changes V, and Both
// changes both axes. Returns true when an index changed.
bool updateSliceIndicesFromPane(
  SliceViewState& state, SlicePane pane, const glm::vec2& uv, SliceCrosshairHit hit);

