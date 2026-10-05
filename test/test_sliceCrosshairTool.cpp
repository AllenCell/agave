#include "renderlib/AppScene.h"
#include "renderlib/Enumerations.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/SliceCrosshairTool.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace {
constexpr uint32_t VOLUME_DIMENSION = 11;
constexpr int VIEWPORT_SIZE = 222;

std::shared_ptr<ImageXYZC>
makeVolume()
{
  constexpr uint64_t voxelCount = static_cast<uint64_t>(VOLUME_DIMENSION) * VOLUME_DIMENSION * VOLUME_DIMENSION;
  constexpr uint64_t byteCount = voxelCount * ImageXYZC::IN_MEMORY_BPP / 8;
  auto* data = new uint8_t[byteCount];
  std::memset(data, 0, byteCount);
  return std::make_shared<ImageXYZC>(VOLUME_DIMENSION,
                                     VOLUME_DIMENSION,
                                     VOLUME_DIMENSION,
                                     1,
                                     static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP),
                                     data,
                                     1.0f,
                                     1.0f,
                                     1.0f,
                                     "units");
}

glm::vec2
panePoint(const SliceRect& pane, float u, float v)
{
  return glm::vec2(pane.x + u * pane.width, pane.y + (1.0f - v) * pane.height);
}

struct SliceCrosshairFixture
{
  SliceCrosshairFixture()
  {
    scene.m_volume = makeVolume();
    settings.m_SliceView.mode = SliceViewMode::Triple;
    settings.m_SliceView.setDimensions(glm::ivec3(VOLUME_DIMENSION));
    settings.m_SliceView.indices = glm::ivec3(5);
    view.scene = &scene;
    view.renderSettings = &settings;
    view.viewport.region = { glm::ivec2(0), glm::ivec2(VIEWPORT_SIZE) };
  }

  Scene scene;
  RenderSettings settings;
  SceneView view;
  SliceCrosshairTool tool;
};
}

TEST_CASE("Slice crosshair tool captures and completes a directional drag", "[sliceView][gesture]")
{
  SliceCrosshairFixture fixture;
  Gesture gesture;
  const TripleSliceLayout layout = fixture.tool.tripleSliceLayout(fixture.view);
  const glm::vec2 pressPosition = panePoint(layout.xy, 0.5f, 0.1f);

  REQUIRE(fixture.tool.hoverHit(fixture.view, pressPosition) == SliceCrosshairHit::Vertical);

  gesture.input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kPress, 0, pressPosition, 1.0);
  fixture.tool.action(fixture.view, gesture);
  REQUIRE(fixture.tool.isDragging());
  REQUIRE(fixture.tool.hoverHit(fixture.view, glm::vec2(-100.0f)) == SliceCrosshairHit::Vertical);
  REQUIRE(fixture.tool.hoverHit(fixture.view, glm::vec2(-100.0f), -1.0f, false) == SliceCrosshairHit::None);

  gesture.input.setPointerPosition(panePoint(layout.xy, 0.9f, 0.1f));
  fixture.tool.action(fixture.view, gesture);
  REQUIRE(fixture.settings.m_SliceView.indices == glm::ivec3(9, 5, 5));
  REQUIRE(fixture.settings.m_DirtyFlags.HasFlag(RenderParamsDirty));

  // The release position is applied even if no final pointer-move event was delivered.
  const glm::vec2 releasePosition = panePoint(layout.xy, 0.7f, 0.1f);
  gesture.input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kRelease, 0, releasePosition, 1.1);
  fixture.tool.action(fixture.view, gesture);
  REQUIRE(fixture.settings.m_SliceView.indices == glm::ivec3(7, 5, 5));
  REQUIRE_FALSE(fixture.tool.isDragging());
  REQUIRE(gesture.input.mbs[Gesture::Input::kButtonLeft].action == Gesture::Input::kNone);
}

TEST_CASE("Slice crosshair tool handles a drag coalesced between render updates", "[sliceView][gesture]")
{
  SliceCrosshairFixture fixture;
  Gesture gesture;
  const TripleSliceLayout layout = fixture.tool.tripleSliceLayout(fixture.view);
  const glm::vec2 pressPosition = panePoint(layout.xy, 0.5f, 0.1f);
  const glm::vec2 releasePosition = panePoint(layout.xy, 0.8f, 0.1f);

  gesture.input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kPress, 0, pressPosition, 1.0);
  gesture.input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kRelease, 0, releasePosition, 1.1);
  fixture.tool.action(fixture.view, gesture);

  REQUIRE(fixture.settings.m_SliceView.indices == glm::ivec3(8, 5, 5));
  REQUIRE_FALSE(fixture.tool.isDragging());
  REQUIRE(gesture.input.mbs[Gesture::Input::kButtonLeft].action == Gesture::Input::kNone);
}

TEST_CASE("Slice crosshair tool recenters both pane axes on a latched double-click", "[sliceView][gesture]")
{
  SliceCrosshairFixture fixture;
  Gesture gesture;
  fixture.settings.m_SliceView.indices = glm::ivec3(0, 0, 3);
  const TripleSliceLayout layout = fixture.tool.tripleSliceLayout(fixture.view);
  const glm::vec2 position = panePoint(layout.xy, 0.2f, 0.8f);

  // Match platforms that report double-click and release before the next update.
  gesture.input.setDoubleClickEvent(Gesture::Input::kButtonLeft, 0, position, 2.0);
  gesture.input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kRelease, 0, position, 2.1);
  fixture.tool.action(fixture.view, gesture);

  REQUIRE(fixture.settings.m_SliceView.indices == glm::ivec3(2, 8, 3));
  REQUIRE(fixture.settings.m_DirtyFlags.HasFlag(RenderParamsDirty));
  REQUIRE_FALSE(fixture.tool.isDragging());
  REQUIRE_FALSE(gesture.input.consumeDoubleClick(Gesture::Input::kButtonLeft));
  REQUIRE(gesture.input.mbs[Gesture::Input::kButtonLeft].action == Gesture::Input::kNone);
}

TEST_CASE("Slice crosshair tool emits screen-space lines and shadows", "[sliceView][gesture]")
{
  SliceCrosshairFixture fixture;
  Gesture gesture;
  int nextSelectionCode = 1;
  fixture.tool.requestCodesRange(&nextSelectionCode);

  fixture.tool.draw(fixture.view, gesture);

  REQUIRE(nextSelectionCode == 1);
  REQUIRE(gesture.graphics.stripRanges.size() == 12);
  REQUIRE(gesture.graphics.stripProjections.size() == 12);
  REQUIRE(gesture.graphics.stripThicknesses.size() == 12);
  REQUIRE(gesture.graphics.stripVerts.size() == 48);
  REQUIRE(std::all_of(gesture.graphics.stripProjections.begin(),
                      gesture.graphics.stripProjections.end(),
                      [](Gesture::Graphics::CommandSequence sequence) {
                        return sequence == Gesture::Graphics::CommandSequence::k2dScreen;
                      }));
  REQUIRE(std::all_of(gesture.graphics.stripVerts.begin(),
                      gesture.graphics.stripVerts.end(),
                      [](const Gesture::Graphics::VertsCode& vertex) {
                        return vertex.s == Gesture::Graphics::k_noSelectionCode;
                      }));

  const int firstShadowVertex = gesture.graphics.stripRanges[0].x;
  const int firstLineVertex = gesture.graphics.stripRanges[2].x;
  REQUIRE(gesture.graphics.stripVerts[firstShadowVertex].r == 0.2f);
  REQUIRE(gesture.graphics.stripVerts[firstLineVertex].r == 1.0f);
}
