#include <catch2/catch_test_macros.hpp>

#include "renderlib/CCamera.h"
#include "renderlib/gesture/gesture.h"

#include <cmath>

namespace
{
constexpr float kEpsilon = 0.000001f;

void
requireNear(const glm::vec3& actual, const glm::vec3& expected)
{
  REQUIRE(glm::all(glm::epsilonEqual(actual, expected, kEpsilon)));
}

CCamera
makePlanarCamera()
{
  CCamera camera;
  camera.m_Projection = ORTHOGRAPHIC;
  camera.m_From = glm::vec3(0.0f, 0.0f, 10.0f);
  camera.m_Target = glm::vec3(0.0f);
  camera.m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
  camera.m_OrthoScale = 2.0f;
  camera.m_SceneBoundingBox = CBoundingBox(glm::vec3(-2.0f, -2.0f, -0.5f), glm::vec3(2.0f, 2.0f, 0.5f));
  camera.Update();
  return camera;
}
}

TEST_CASE("Gesture wheel input accumulates until consumed", "[gesture][camera]")
{
  Gesture::Input input;

  input.addWheelDelta(0.25f);
  input.addWheelDelta(0.75f);
  REQUIRE(glm::epsilonEqual(input.consumeWheelDelta(), 1.0f, kEpsilon));
  REQUIRE(input.consumeWheelDelta() == 0.0f);

  input.addWheelDelta(2.0f);
  input.consume();
  REQUIRE(input.consumeWheelDelta() == 0.0f);
}

TEST_CASE("Gesture double-click input is a one-frame latch", "[gesture][camera]")
{
  Gesture::Input input;
  const glm::vec2 position(12.0f, 34.0f);

  SECTION("An explicit event survives the following release and is consumed once")
  {
    input.setDoubleClickEvent(Gesture::Input::kButtonLeft, Gesture::Input::kCtrl, position, 1.0);
    input.setButtonEvent(
      Gesture::Input::kButtonLeft, Gesture::Input::kRelease, Gesture::Input::kCtrl, position, 1.1);

    REQUIRE_FALSE(input.consumeDoubleClick(Gesture::Input::kButtonLeft, Gesture::Input::kAlt));
    REQUIRE(input.consumeDoubleClick(Gesture::Input::kButtonLeft, Gesture::Input::kCtrl));
    REQUIRE_FALSE(input.consumeDoubleClick(Gesture::Input::kButtonLeft, Gesture::Input::kCtrl));
  }

  SECTION("An unhandled explicit event expires at the end of the frame")
  {
    input.setDoubleClickEvent(Gesture::Input::kButtonLeft, 0, position, 1.0);
    input.consume();
    REQUIRE_FALSE(input.consumeDoubleClick(Gesture::Input::kButtonLeft));
  }

  SECTION("Timestamp inference remains available for adapters that report two presses")
  {
    input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kPress, 0, position, 2.0);
    REQUIRE_FALSE(input.consumeDoubleClick(Gesture::Input::kButtonLeft));
    input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kRelease, 0, position, 2.1);
    input.consume();

    input.setButtonEvent(Gesture::Input::kButtonLeft, Gesture::Input::kPress, 0, position, 2.2);
    REQUIRE(input.consumeDoubleClick(Gesture::Input::kButtonLeft));
  }
}

TEST_CASE("Planar camera manipulation pans without rotating", "[gesture][camera]")
{
  CCamera camera = makePlanarCamera();
  Gesture gesture;
  CameraModifier cameraMod;
  const glm::vec2 viewportSize(200.0f, 100.0f);

  gesture.input.setButtonEvent(
    Gesture::Input::kButtonLeft, Gesture::Input::kPress, 0, glm::vec2(0.0f), 1.0);
  gesture.input.setPointerPosition(glm::vec2(50.0f, 25.0f));

  REQUIRE(cameraManipulation(
    viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));

  // The orthographic view spans four world units over 100 pixels vertically.
  // Camera-space right points toward negative world X for this camera orientation.
  requireNear(cameraMod.position, glm::vec3(-2.0f, 1.0f, 0.0f));
  requireNear(cameraMod.target, cameraMod.position);
  requireNear(cameraMod.up, glm::vec3(0.0f));
  requireNear(camera.m_Up, glm::vec3(0.0f, 1.0f, 0.0f));

  gesture.input.setButtonEvent(
    Gesture::Input::kButtonLeft, Gesture::Input::kRelease, 0, glm::vec2(50.0f, 25.0f), 1.1);
  cameraMod = CameraModifier();
  REQUIRE(cameraManipulation(
    viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));
  requireNear(camera.m_From, glm::vec3(-2.0f, 1.0f, 10.0f));
  requireNear(camera.m_Target, glm::vec3(-2.0f, 1.0f, 0.0f));
  REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 2.0f, kEpsilon));
}

TEST_CASE("Planar camera manipulation zooms without rotating", "[gesture][camera]")
{
  const glm::vec2 viewportSize(200.0f, 100.0f);

  SECTION("Right drag uses camera dolly")
  {
    CCamera camera = makePlanarCamera();
    Gesture gesture;
    CameraModifier cameraMod;

    gesture.input.setButtonEvent(
      Gesture::Input::kButtonRight, Gesture::Input::kPress, 0, glm::vec2(0.0f), 1.0);
    gesture.input.setButtonEvent(
      Gesture::Input::kButtonRight, Gesture::Input::kRelease, 0, glm::vec2(0.0f, 70.0f), 1.1);

    REQUIRE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));

    const float factor = std::exp(0.7f);
    requireNear(camera.m_From, glm::vec3(0.0f, 0.0f, 10.0f * factor));
    requireNear(camera.m_Target, glm::vec3(0.0f));
    requireNear(camera.m_Up, glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 2.0f * factor, kEpsilon));
  }

  SECTION("Horizontal right drag does not zoom")
  {
    CCamera camera = makePlanarCamera();
    Gesture gesture;
    CameraModifier cameraMod;

    gesture.input.setButtonEvent(
      Gesture::Input::kButtonRight, Gesture::Input::kPress, 0, glm::vec2(0.0f), 1.0);
    gesture.input.setButtonEvent(
      Gesture::Input::kButtonRight, Gesture::Input::kRelease, 0, glm::vec2(70.0f, 0.0f), 1.1);

    REQUIRE_FALSE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));
    requireNear(camera.m_From, glm::vec3(0.0f, 0.0f, 10.0f));
    REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 2.0f, kEpsilon));
  }

  SECTION("Wheel input is applied once")
  {
    CCamera camera = makePlanarCamera();
    Gesture gesture;
    CameraModifier cameraMod;
    gesture.input.addWheelDelta(1.0f);

    REQUIRE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));

    const float factor = std::exp(-0.15f);
    requireNear(camera.m_From, glm::vec3(0.0f, 0.0f, 10.0f * factor));
    REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 2.0f * factor, kEpsilon));
    REQUIRE(gesture.input.consumeWheelDelta() == 0.0f);

    cameraMod = CameraModifier();
    REQUIRE_FALSE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));
  }

  SECTION("Zoom is bounded relative to the fitted view")
  {
    CCamera camera = makePlanarCamera();
    Gesture gesture;
    CameraModifier cameraMod;
    gesture.input.addWheelDelta(10000.0f);
    REQUIRE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));
    REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 0.02f, kEpsilon));

    camera = makePlanarCamera();
    gesture.input.addWheelDelta(-10000.0f);
    REQUIRE(cameraManipulation(
      viewportSize, gesture, camera, cameraMod, CameraManipulationMode::Planar2D));
    REQUIRE(glm::epsilonEqual(camera.m_OrthoScale, 40.0f, kEpsilon));
  }
}
