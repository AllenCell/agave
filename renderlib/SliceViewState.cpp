#include "SliceViewState.h"

#include <algorithm>
#include <cmath>

namespace
{
const SliceRect EMPTY_RECT;

int
clampIndex(int index, int dimension)
{
  return std::clamp(index, 0, std::max(1, dimension) - 1);
}

float
indexToUv(int index, int dimension)
{
  if (dimension <= 1) {
    return 0.5f;
  }
  return static_cast<float>(clampIndex(index, dimension)) / static_cast<float>(dimension - 1);
}

int
uvToIndex(float uv, int dimension)
{
  if (dimension <= 1) {
    return 0;
  }
  return clampIndex(static_cast<int>(std::lround(std::clamp(uv, 0.0f, 1.0f) * (dimension - 1))), dimension);
}

void
paneAxes(SlicePane pane, int& uAxis, int& vAxis)
{
  switch (pane) {
    case SlicePane::XY:
      uAxis = 0;
      vAxis = 1;
      break;
    case SlicePane::YZ:
      uAxis = 2;
      vAxis = 1;
      break;
    case SlicePane::XZ:
      uAxis = 0;
      vAxis = 2;
      break;
    default:
      uAxis = -1;
      vAxis = -1;
      break;
  }
}
}

bool
SliceRect::isValid() const
{
  return width > 0.0f && height > 0.0f;
}

bool
SliceRect::contains(const glm::vec2& point) const
{
  return isValid() && point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
}

const SliceRect&
TripleSliceLayout::pane(SlicePane paneId) const
{
  switch (paneId) {
    case SlicePane::XY:
      return xy;
    case SlicePane::YZ:
      return yz;
    case SlicePane::XZ:
      return xz;
    default:
      return EMPTY_RECT;
  }
}

void
SliceViewState::setDimensions(const glm::ivec3& newDimensions, bool centerIndices)
{
  dimensions = glm::max(newDimensions, glm::ivec3(1));
  if (centerIndices) {
    indices = dimensions / 2;
    clampIndices();
  } else {
    clampIndices();
  }
}

void
SliceViewState::clampIndices()
{
  indices.x = clampIndex(indices.x, dimensions.x);
  indices.y = clampIndex(indices.y, dimensions.y);
  indices.z = clampIndex(indices.z, dimensions.z);
}

void
SliceViewState::resetView()
{
  if (!isSingleSlice()) {
    return;
  }
  activePan() = glm::vec2(0.0f);
  activeZoom() = 1.0f;
}

glm::vec2&
SliceViewState::activePan()
{
  return pan[mode == SliceViewMode::Triple ? 0 : static_cast<std::size_t>(mode)];
}

const glm::vec2&
SliceViewState::activePan() const
{
  return pan[mode == SliceViewMode::Triple ? 0 : static_cast<std::size_t>(mode)];
}

float&
SliceViewState::activeZoom()
{
  return zoom[mode == SliceViewMode::Triple ? 0 : static_cast<std::size_t>(mode)];
}

float
SliceViewState::activeZoom() const
{
  return zoom[mode == SliceViewMode::Triple ? 0 : static_cast<std::size_t>(mode)];
}

bool
SliceViewState::isSingleSlice() const
{
  return mode != SliceViewMode::Triple;
}

int
SliceViewState::activeAxis() const
{
  switch (mode) {
    case SliceViewMode::X:
      return 0;
    case SliceViewMode::Y:
      return 1;
    case SliceViewMode::Z:
      return 2;
    default:
      return -1;
  }
}

int
SliceViewState::activeIndex() const
{
  const int axis = activeAxis();
  return axis < 0 ? 0 : indices[axis];
}

int
SliceViewState::activeDimension() const
{
  const int axis = activeAxis();
  return axis < 0 ? 1 : dimensions[axis];
}

void
SliceViewState::setActiveIndex(int index)
{
  const int axis = activeAxis();
  if (axis >= 0) {
    indices[axis] = clampIndex(index, dimensions[axis]);
  }
}

TripleSliceLayout
computeTripleSliceLayout(const glm::vec3& physicalDimensions, const glm::ivec2& viewportSize, float gapPixels)
{
  TripleSliceLayout result;
  const float viewportWidth = static_cast<float>(viewportSize.x);
  const float viewportHeight = static_cast<float>(viewportSize.y);
  const float x = physicalDimensions.x;
  const float y = physicalDimensions.y;
  const float z = physicalDimensions.z;
  const float gap = std::max(0.0f, gapPixels);

  if (viewportWidth <= gap || viewportHeight <= gap || x <= 0.0f || y <= 0.0f || z <= 0.0f) {
    return result;
  }

  const float scale = std::min((viewportWidth - gap) / (x + z), (viewportHeight - gap) / (y + z));
  if (!(scale > 0.0f)) {
    return result;
  }

  // Keep every anatomical plane addressable even when a highly anisotropic
  // dimension would round below one screen pixel. Without this floor, a
  // one-voxel-thick Z dimension can invalidate the otherwise useful XY pane.
  const float xyWidth = std::max(1.0f, std::floor(x * scale));
  const float xyHeight = std::max(1.0f, std::floor(y * scale));
  const float yzWidth = std::max(1.0f, std::floor(z * scale));
  const float yzHeight = xyHeight;
  const float xzWidth = xyWidth;
  const float xzHeight = std::max(1.0f, std::floor(z * scale));

  const float totalWidth = xyWidth + gap + yzWidth;
  const float totalHeight = xyHeight + gap + xzHeight;
  const float offsetX = std::floor((viewportWidth - totalWidth) * 0.5f);
  const float offsetBottom = std::floor((viewportHeight - totalHeight) * 0.5f);
  // Vol-E computes the layout in bottom-left framebuffer coordinates. Keep
  // that rounding convention, then convert to Qt's top-left coordinates. This
  // matters by one pixel when the unused vertical space is odd.
  const float offsetTop = viewportHeight - totalHeight - offsetBottom;

  // Convert the reference bottom-left pane layout into Qt's top-left space.
  result.xz = { offsetX, offsetTop, xzWidth, xzHeight };
  result.xy = { offsetX, offsetTop + xzHeight + gap, xyWidth, xyHeight };
  result.yz = { offsetX + xyWidth + gap, offsetTop + xzHeight + gap, yzWidth, yzHeight };
  return result;
}

SlicePane
slicePaneAt(const TripleSliceLayout& layout, const glm::vec2& point)
{
  if (layout.xy.contains(point)) {
    return SlicePane::XY;
  }
  if (layout.yz.contains(point)) {
    return SlicePane::YZ;
  }
  if (layout.xz.contains(point)) {
    return SlicePane::XZ;
  }
  return SlicePane::None;
}

glm::vec2
slicePaneUv(const SliceRect& pane, const glm::vec2& point)
{
  if (!pane.isValid()) {
    return glm::vec2(0.0f);
  }
  const float u = std::clamp((point.x - pane.x) / pane.width, 0.0f, 1.0f);
  const float v = std::clamp(1.0f - (point.y - pane.y) / pane.height, 0.0f, 1.0f);
  return glm::vec2(u, v);
}

glm::vec2
sliceCrosshairUv(const SliceViewState& state, SlicePane pane)
{
  int uAxis = -1;
  int vAxis = -1;
  paneAxes(pane, uAxis, vAxis);
  if (uAxis < 0) {
    return glm::vec2(0.0f);
  }
  return glm::vec2(
    indexToUv(state.indices[uAxis], state.dimensions[uAxis]), indexToUv(state.indices[vAxis], state.dimensions[vAxis]));
}

SliceCrosshairHit
hitTestSliceCrosshair(const SliceViewState& state,
                      SlicePane pane,
                      const TripleSliceLayout& layout,
                      const glm::vec2& point,
                      float thresholdPixels)
{
  const SliceRect& rect = layout.pane(pane);
  if (!rect.contains(point)) {
    return SliceCrosshairHit::None;
  }

  const glm::vec2 crosshair = sliceCrosshairUv(state, pane);
  const float crosshairX = rect.x + crosshair.x * rect.width;
  const float crosshairY = rect.y + (1.0f - crosshair.y) * rect.height;
  const bool onVertical = std::abs(point.x - crosshairX) <= thresholdPixels;
  const bool onHorizontal = std::abs(point.y - crosshairY) <= thresholdPixels;
  if (onVertical && onHorizontal) {
    return SliceCrosshairHit::Both;
  }
  if (onVertical) {
    return SliceCrosshairHit::Vertical;
  }
  if (onHorizontal) {
    return SliceCrosshairHit::Horizontal;
  }
  return SliceCrosshairHit::None;
}

bool
updateSliceIndicesFromPane(SliceViewState& state, SlicePane pane, const glm::vec2& uv, SliceCrosshairHit hit)
{
  int uAxis = -1;
  int vAxis = -1;
  paneAxes(pane, uAxis, vAxis);
  if (uAxis < 0 || hit == SliceCrosshairHit::None) {
    return false;
  }

  const glm::ivec3 oldIndices = state.indices;
  if (hit == SliceCrosshairHit::Vertical || hit == SliceCrosshairHit::Both) {
    state.indices[uAxis] = uvToIndex(uv.x, state.dimensions[uAxis]);
  }
  if (hit == SliceCrosshairHit::Horizontal || hit == SliceCrosshairHit::Both) {
    state.indices[vAxis] = uvToIndex(uv.y, state.dimensions[vAxis]);
  }
  return state.indices != oldIndices;
}

