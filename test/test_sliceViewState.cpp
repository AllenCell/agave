#include <catch2/catch_test_macros.hpp>

#include "renderlib/SliceViewState.h"

TEST_CASE("Slice view state clamps and selects its active axis", "[sliceView]")
{
  SliceViewState state;
  state.indices = glm::ivec3(10, -3, 99);
  state.setDimensions(glm::ivec3(8, 9, 10));
  REQUIRE(state.indices == glm::ivec3(7, 0, 9));

  state.mode = SliceViewMode::Y;
  REQUIRE(state.isSingleSlice());
  REQUIRE(state.activeAxis() == 1);
  REQUIRE(state.activeDimension() == 9);
  state.setActiveIndex(6);
  REQUIRE(state.indices == glm::ivec3(7, 6, 9));

  state.mode = SliceViewMode::Triple;
  REQUIRE_FALSE(state.isSingleSlice());
  REQUIRE(state.activeAxis() == -1);
}

TEST_CASE("Slice view dimensions can center indices", "[sliceView]")
{
  SliceViewState state;
  state.setDimensions(glm::ivec3(10, 5, 2), true);
  REQUIRE(state.dimensions == glm::ivec3(10, 5, 2));
  REQUIRE(state.indices == glm::ivec3(5, 2, 1));
}

TEST_CASE("Triple slice layout preserves physical pane proportions", "[sliceView]")
{
  const TripleSliceLayout layout = computeTripleSliceLayout(glm::vec3(2.0f, 1.0f, 1.0f), glm::ivec2(310, 210), 10.0f);

  REQUIRE(layout.xy.isValid());
  REQUIRE(layout.xy.width == 200.0f);
  REQUIRE(layout.xy.height == 100.0f);
  REQUIRE(layout.yz.width == 100.0f);
  REQUIRE(layout.yz.height == 100.0f);
  REQUIRE(layout.xz.width == 200.0f);
  REQUIRE(layout.xz.height == 100.0f);
  REQUIRE(layout.xz.y < layout.xy.y);
  REQUIRE(layout.yz.x > layout.xy.x);

  REQUIRE(slicePaneAt(layout, glm::vec2(layout.xy.x + 1.0f, layout.xy.y + 1.0f)) == SlicePane::XY);
  REQUIRE(slicePaneAt(layout, glm::vec2(layout.yz.x + 1.0f, layout.yz.y + 1.0f)) == SlicePane::YZ);
  REQUIRE(slicePaneAt(layout, glm::vec2(-1.0f, -1.0f)) == SlicePane::None);
}

TEST_CASE("Triple slice layout preserves Vol-E bottom-left centering when converted for Qt", "[sliceView]")
{
  // This viewport leaves one unused vertical pixel. Vol-E assigns it above
  // the bottom-left layout; after converting to Qt coordinates it appears as
  // a one-pixel top offset.
  const TripleSliceLayout layout =
    computeTripleSliceLayout(glm::vec3(2.0f, 1.0f, 1.0f), glm::ivec2(310, 211), 10.0f);
  REQUIRE(layout.xz.y == 1.0f);
  REQUIRE(layout.xy.y == 111.0f);
  REQUIRE(layout.yz.y == 111.0f);
}

TEST_CASE("Triple slice layout keeps anisotropic one-voxel planes visible", "[sliceView]")
{
  const TripleSliceLayout layout =
    computeTripleSliceLayout(glm::vec3(2048.0f, 2048.0f, 1.0f), glm::ivec2(800, 800), 2.0f);

  REQUIRE(layout.xy.isValid());
  REQUIRE(layout.yz.isValid());
  REQUIRE(layout.xz.isValid());
  REQUIRE(layout.yz.width == 1.0f);
  REQUIRE(layout.xz.height == 1.0f);
}

TEST_CASE("Triple slice crosshair maps pane axes", "[sliceView]")
{
  SliceViewState state;
  state.mode = SliceViewMode::Triple;
  state.setDimensions(glm::ivec3(11, 21, 31));
  state.indices = glm::ivec3(5, 10, 15);

  REQUIRE(sliceCrosshairUv(state, SlicePane::XY) == glm::vec2(0.5f, 0.5f));
  REQUIRE(sliceCrosshairUv(state, SlicePane::YZ) == glm::vec2(0.5f, 0.5f));
  REQUIRE(sliceCrosshairUv(state, SlicePane::XZ) == glm::vec2(0.5f, 0.5f));


  REQUIRE(updateSliceIndicesFromPane(state, SlicePane::XY, glm::vec2(0.2f, 0.8f), SliceCrosshairHit::Both));
  REQUIRE(state.indices == glm::ivec3(2, 16, 15));

  REQUIRE(updateSliceIndicesFromPane(state, SlicePane::YZ, glm::vec2(1.0f, 0.0f), SliceCrosshairHit::Vertical));
  REQUIRE(state.indices == glm::ivec3(2, 16, 30));

  REQUIRE(updateSliceIndicesFromPane(state, SlicePane::XZ, glm::vec2(0.4f, 0.1f), SliceCrosshairHit::Horizontal));
  REQUIRE(state.indices == glm::ivec3(2, 16, 3));
}

TEST_CASE("Triple slice crosshair hit testing follows line orientation", "[sliceView]")
{
  SliceViewState state;
  state.setDimensions(glm::ivec3(11));
  state.indices = glm::ivec3(5);

  TripleSliceLayout layout;
  layout.xy = { 10.0f, 20.0f, 100.0f, 80.0f };
  REQUIRE(hitTestSliceCrosshair(state, SlicePane::XY, layout, glm::vec2(61.0f, 50.0f), 2.0f) ==
          SliceCrosshairHit::Vertical);
  REQUIRE(hitTestSliceCrosshair(state, SlicePane::XY, layout, glm::vec2(40.0f, 59.0f), 2.0f) ==
          SliceCrosshairHit::Horizontal);
  REQUIRE(hitTestSliceCrosshair(state, SlicePane::XY, layout, glm::vec2(60.0f, 60.0f), 2.0f) ==
          SliceCrosshairHit::Both);
}

