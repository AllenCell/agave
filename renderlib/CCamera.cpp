#include "CCamera.h"

#include "gesture/gesture.h"

void
CCamera::SetViewMode(const EViewMode ViewMode)
{
  if (ViewMode == ViewModeUser)
    return;

  glm::vec3 ctr = m_SceneBoundingBox.GetCenter();
  m_Target = ctr;
  m_Up = glm::vec3(0.0f, 1.0f, 0.0f);

  const float size = m_SceneBoundingBox.GetDiagonalLength();
  const float Length = (m_Projection == ORTHOGRAPHIC) ? 2.0f : size * 0.5f / tan(0.5f * m_FovV * DEG_TO_RAD);
  m_OrthoScale = DEF_ORTHO_SCALE;

  m_From = m_Target;

  switch (ViewMode) {
    case ViewModeFront:
      m_From.z += Length;
      m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case ViewModeBack:
      m_From.z -= Length;
      m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case ViewModeLeft:
      m_From.x += Length;
      m_Up = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case ViewModeRight:
      m_From.x -= Length;
      m_Up = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case ViewModeTop:
      m_From.y += Length;
      m_Up = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case ViewModeBottom:
      m_From.y -= Length;
      m_Up = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case ViewModeIsometricFrontLeftTop:
      m_From = glm::vec3(Length, Length, -Length);
      break;
    case ViewModeIsometricFrontRightTop:
      m_From = m_Target + glm::vec3(-Length, Length, -Length);
      break;
    case ViewModeIsometricFrontLeftBottom:
      m_From = m_Target + glm::vec3(Length, -Length, -Length);
      break;
    case ViewModeIsometricFrontRightBottom:
      m_From = m_Target + glm::vec3(-Length, -Length, -Length);
      break;
    case ViewModeIsometricBackLeftTop:
      m_From = m_Target + glm::vec3(Length, Length, Length);
      break;
    case ViewModeIsometricBackRightTop:
      m_From = m_Target + glm::vec3(-Length, Length, Length);
      break;
    case ViewModeIsometricBackLeftBottom:
      m_From = m_Target + glm::vec3(Length, -Length, Length);
      break;
    case ViewModeIsometricBackRightBottom:
      m_From = m_Target + glm::vec3(-Length, -Length, Length);
      break;
    default:
      break;
  }

  Update();
}

LinearSpace3f
CCamera::getFrame() const
{
  // LOG_DEBUG << "CCamera::getFrame()" << glm::to_string(m_U) << ", " << glm::to_string(m_V) << ", "
  //           << glm::to_string(-m_N) << "\n";

  // see glm::lookat for similar negation of the z vector (opengl convention)
  return { m_U, m_V, -m_N };
}

// this implementation comes from an old unity3d wiki page.
// Two non-parallel lines which may or may not touch each other have a point on each line which are closest
// to each other. This function finds those two points. If the lines are not parallel, the function
// outputs true, otherwise false.
bool
FindClosestPointsOnTwoLines(Ray line1, Ray line2, glm::vec3& closestPointLine1, glm::vec3& closestPointLine2)
{
  closestPointLine1 = glm::vec3(0);
  closestPointLine2 = glm::vec3(0);

  glm::vec3 line1Direction = line1.direction;
  glm::vec3 line2Direction = line2.direction;

  float a = glm::dot(line1Direction, line1Direction);
  float b = glm::dot(line1Direction, line2Direction);
  float e = glm::dot(line2Direction, line2Direction);

  float d = a * e - b * b;
  if (d == 0.0f) {
    return false;
  }
  // if d==0 then lines are parallel and this is a big fail.

  glm::vec3 r = line1.origin - line2.origin;
  float c = glm::dot(line1Direction, r);
  float f = glm::dot(line2Direction, r);

  float s = (b * f - c * e) / d;
  float t = (a * f - c * b) / d;

  closestPointLine1 = line1.origin + line1Direction * s;
  closestPointLine2 = line2.origin + line2Direction * t;
  return true;
}

bool
IsOutermostPointInDirection(int pointIndex,
                            const glm::vec3& direction,
                            const std::array<glm::vec3, NUM_BBOX_CORNERS>& boundingBoxPoints)
{
  glm::vec3 point = boundingBoxPoints[pointIndex];
  for (int i = 0; i < NUM_BBOX_CORNERS; i++) {
    if (i != pointIndex && glm::dot(direction, boundingBoxPoints[i] - point) > 0) {
      return false;
    }
  }

  return true;
}

void
CCamera::ComputeFitToBounds(const CBoundingBox& sceneBBox, glm::vec3& newPosition, glm::vec3& newTarget) const
{
  newTarget = sceneBBox.GetCenter();

  float padding = sceneBBox.GetDiagonalLength() * 0.025f;

  // assume bbox, padding and cam in world space.

  // make a copy of the bounds and add padding
  CBoundingBox bounds = sceneBBox;
  if (padding != 0.0f) {
    bounds.Extend(padding);
  }
  glm::vec3 boundsCenter = bounds.GetCenter();
  std::array<glm::vec3, NUM_BBOX_CORNERS> boundingBoxPoints;
  bounds.GetCorners(boundingBoxPoints);

  glm::vec3 cameraDirection = glm::normalize(m_Target - m_From);

  if (m_Projection == ORTHOGRAPHIC) {
    // orthographic mode strategy: for ortho mode, find the left, top, bottom and rightmost points of the bounding box.
    // the following assumes no scaling between camera and world space
    glm::mat4 viewMatrix;
    getViewMatrix(viewMatrix);
    // transform into camera space
    for (int i = 0; i < NUM_BBOX_CORNERS; i++) {
      boundingBoxPoints[i] = viewMatrix * glm::vec4(boundingBoxPoints[i], 1.0f);
    }

    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;

    for (int i = 0; i < NUM_BBOX_CORNERS; i++) {
      glm::vec3 localPoint = (boundingBoxPoints[i]);
      if (localPoint.x < minX) {
        minX = localPoint.x;
      }
      if (localPoint.x > maxX) {
        maxX = localPoint.x;
      }
      if (localPoint.y < minY) {
        minY = localPoint.y;
      }
      if (localPoint.y > maxY) {
        maxY = localPoint.y;
      }
    }

    // the math here is a bit backwards. Rather than computing a new orthoScale,
    // we are computing a camera position that will RESULT in a new orthoScale value when
    // applied via operator+=(CCamera& camera, const CameraModifier& mod)
    float aspect = (float)m_Film.GetWidth() / (float)m_Film.GetHeight();
    float oldDistance = glm::length(m_Target - m_From);
    float newOrthoScale = std::max(maxY - minY, (maxX - minX) / aspect) * 0.5f;
    float newDistance = oldDistance * newOrthoScale / m_OrthoScale;

    newPosition = boundsCenter - cameraDirection * newDistance;
  } else {
    // perspective mode strategy:
    // we will find the outermost points of the bounding box in the direction of the camera's top, bottom, left and
    // right frustum planes.
    // We will compute new frustum planes using those outermost points, and find
    // the intersection of top/bottom planes, and left/right planes.
    // Then we will find the camera distance that is the best choice on the line between the two plane intersections.

    float verticalFOV = m_FovV * 0.5f * DEG_TO_RAD; // radians
    float horizontalFOV = GetHorizontalFOV_radians() * 0.5f;

    // Normals of the camera's frustum planes.
    // recall that m_V is the up direction and m_U is the right direction.
    glm::vec3 topFrustumPlaneNormal = glm::rotate(m_V, verticalFOV, m_U);
    glm::vec3 bottomFrustumPlaneNormal = glm::rotate(-m_V, -verticalFOV, m_U);
    glm::vec3 rightFrustumPlaneNormal = glm::rotate(m_U, -horizontalFOV, m_V);
    glm::vec3 leftFrustumPlaneNormal = glm::rotate(-m_U, horizontalFOV, m_V);

    // Credit for algorithm: https://stackoverflow.com/a/66113254/2373034
    // 1. Find edge points of the bounds using the camera's frustum planes
    // 2. Create a plane for each edge point that goes through the point and has the corresponding frustum plane's
    // normal
    // 3. Find the intersection line of horizontal edge points' planes (horizontalIntersection) and vertical edge
    // points' planes (verticalIntersection)
    //    If we move the camera along horizontalIntersection, the bounds will always with the camera's width perfectly
    //    (similar effect goes for verticalIntersection)
    // 4. Find the closest line segment between these two lines (horizontalIntersection and verticalIntersection) and
    // place the camera at the farthest point on that line
    int leftmostPoint = -1, rightmostPoint = -1, topmostPoint = -1, bottommostPoint = -1;
    for (int i = 0; i < NUM_BBOX_CORNERS; i++) {
      if (leftmostPoint < 0 && IsOutermostPointInDirection(i, leftFrustumPlaneNormal, boundingBoxPoints)) {
        leftmostPoint = i;
      }
      if (rightmostPoint < 0 && IsOutermostPointInDirection(i, rightFrustumPlaneNormal, boundingBoxPoints)) {
        rightmostPoint = i;
      }
      if (topmostPoint < 0 && IsOutermostPointInDirection(i, topFrustumPlaneNormal, boundingBoxPoints)) {
        topmostPoint = i;
      }
      if (bottommostPoint < 0 && IsOutermostPointInDirection(i, bottomFrustumPlaneNormal, boundingBoxPoints)) {
        bottommostPoint = i;
      }
    }

    Ray horizontalIntersection =
      Plane::GetIntersection(Plane(leftFrustumPlaneNormal, boundingBoxPoints[leftmostPoint]),
                             Plane(rightFrustumPlaneNormal, boundingBoxPoints[rightmostPoint]));
    Ray verticalIntersection =
      Plane::GetIntersection(Plane(topFrustumPlaneNormal, boundingBoxPoints[topmostPoint]),
                             Plane(bottomFrustumPlaneNormal, boundingBoxPoints[bottommostPoint]));

    glm::vec3 closestPoint1, closestPoint2;
    FindClosestPointsOnTwoLines(horizontalIntersection, verticalIntersection, closestPoint1, closestPoint2);

    newPosition = glm::dot(closestPoint1 - closestPoint2, cameraDirection) < 0 ? closestPoint1 : closestPoint2;
  }
}

namespace {

constexpr float MIN_PLANAR_ZOOM = 0.05f;
constexpr float MAX_PLANAR_ZOOM = 100.0f;
constexpr float MIN_ORTHO_SCALE = 0.000001f;

float
planarFitScale(const CCamera& camera, const glm::vec2& viewportSize)
{
  if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
    return glm::max(camera.m_OrthoScale, MIN_ORTHO_SCALE);
  }

  const glm::vec3 extent = camera.m_SceneBoundingBox.GetExtent();
  const float planeWidth = glm::dot(glm::abs(camera.m_U), extent);
  const float planeHeight = glm::dot(glm::abs(camera.m_V), extent);
  const float aspect = viewportSize.x / viewportSize.y;
  return glm::max(0.5f * glm::max(planeHeight, planeWidth / aspect), MIN_ORTHO_SCALE);
}

float
boundedPlanarZoomFactor(const CCamera& camera, const glm::vec2& viewportSize, float requestedFactor)
{
  const float fitScale = planarFitScale(camera, viewportSize);
  const float minScale = fitScale / MAX_PLANAR_ZOOM;
  const float maxScale = fitScale / MIN_PLANAR_ZOOM;
  const float currentScale = glm::clamp(camera.m_OrthoScale, minScale, maxScale);
  const float requestedScale = currentScale * requestedFactor;
  return glm::clamp(requestedScale, minScale, maxScale) / currentScale;
}

} // namespace

glm::vec3
cameraTrack(glm::vec2 drag, CCamera& camera, const glm::vec2 viewportSize)
{
  float width = viewportSize.x;
  glm::vec3 v = camera.m_From - camera.m_Target;

  float dragScale = 0.0f;
  if (camera.m_Projection == ORTHOGRAPHIC) {
    // m_OrthoScale is the vertical half-extent of the projection, so the full
    // viewport height spans twice that many world units.
    const float height = viewportSize.y;
    if (height <= 0.0f) {
      return glm::vec3(0);
    }
    dragScale = camera.m_OrthoScale / (height * 0.5f);
  } else {
    if (width <= 0.0f) {
      return glm::vec3(0);
    }

    // Project the drag movement in pixels to the image plane set at the distance of the
    // camera target.
    const float distance = length(v);
    const float halfHorizontalAperture = camera.getHalfHorizontalAperture();
    dragScale = distance * halfHorizontalAperture / (width * 0.5f);
  }

  drag *= dragScale;
  glm::vec3 x = glm::normalize(glm::cross(v, camera.m_Up));
  glm::vec3 y = glm::normalize(glm::cross(x, v));
  glm::vec3 track = x * drag.x + y * drag.y;

  return track;
}

bool
cameraManipulationTrack(const glm::vec2 viewportSize,
                        Gesture::Input::Button& button,
                        CCamera& camera,
                        CameraModifier& cameraMod)
{
  bool cameraEdit = true;
  glm::vec2 drag = button.drag;
  if (button.dragConstraint == Gesture::Input::kHorizontal) {
    drag.y = 0;
  }
  if (button.dragConstraint == Gesture::Input::kVertical) {
    drag.x = 0;
  }

  if (drag.x == 0 && drag.y == 0) {
    cameraEdit = false;
  }

  glm::vec3 track = cameraTrack(drag, camera, viewportSize);

  if (button.action == Gesture::Input::kPress || button.action == Gesture::Input::kDrag) {
    cameraMod.position = track;
    cameraMod.target = track;
  } else if (button.action == Gesture::Input::kRelease) {
    camera.m_From += track;
    camera.m_Target += track;

    // Consume pointer button release event
    Gesture::Input::reset(button);
  }
  return cameraEdit;
}
bool
cameraManipulationDolly(const glm::vec2 viewportSize,
                        Gesture::Input::Button& button,
                        CCamera& camera,
                        CameraModifier& cameraMod,
                        bool allowLinearMotion = true,
                        bool verticalOnly = false,
                        float pixelsPerUnit = 700.0f,
                        float exponentialDirection = -1.0f)
{

  bool cameraEdit = true;
  const float dragScale = 1.0f / pixelsPerUnit;

  glm::vec2 drag = button.drag;
  float dragDist = verticalOnly ? drag.y : drag.x + drag.y;
  glm::vec3 v = camera.m_From - camera.m_Target;
  glm::vec3 motion, targetMotion;

  if (dragDist == 0.0f) {
    cameraEdit = false;
  }

  float factor = 1.0f;
  if (!allowLinearMotion || (button.modifier & Gesture::Input::kShift) == 0) {
    // Exponential motion, the closer to the target, the slower the motion,
    // the further away the faster.
    factor = expf(exponentialDirection * dragDist * dragScale);
    if (!allowLinearMotion && camera.m_Projection == ORTHOGRAPHIC) {
      factor = boundedPlanarZoomFactor(camera, viewportSize, factor);
    }
    glm::vec3 v_scaled = v * factor;
    motion = v_scaled - v;
    targetMotion = glm::vec3(0);
  } else {
    // Linear motion. We move position and target at once. This mode allows the user not
    // to get stuck with a camera that doesn't move because the position got too close to
    // the target.
    factor = (-dragDist * dragScale);
    glm::vec3 v_scaled = v * factor;
    motion = v_scaled;
    targetMotion = motion;
  }

  if (button.action == Gesture::Input::kPress || button.action == Gesture::Input::kDrag) {
    cameraMod.position = motion;
    cameraMod.target = targetMotion;
  } else if (button.action == Gesture::Input::kRelease) {
    camera.m_From += motion;
    camera.m_Target += targetMotion;
    camera.m_OrthoScale *= factor;
    camera.Update();

    // Consume gesture on button release
    Gesture::Input::reset(button);
  }
  return cameraEdit;
}

bool
cameraManipulationWheel(const glm::vec2 viewportSize, Gesture::Input& input, CCamera& camera)
{
  const float wheelDelta = input.consumeWheelDelta();
  if (wheelDelta == 0.0f) {
    return false;
  }

  // Positive wheel movement zooms in. Scale both camera distance and the
  // orthographic projection so this has equivalent behavior in either projection.
  static constexpr float WHEEL_ZOOM_EXPONENT_PER_STEP = 0.15f;
  float factor = expf(-wheelDelta * WHEEL_ZOOM_EXPONENT_PER_STEP);
  if (camera.m_Projection == ORTHOGRAPHIC) {
    factor = boundedPlanarZoomFactor(camera, viewportSize, factor);
  }
  const glm::vec3 reverseLineOfSight = camera.m_From - camera.m_Target;
  camera.m_From = camera.m_Target + reverseLineOfSight * factor;
  camera.m_OrthoScale *= factor;
  camera.Update();
  return true;
}

bool
cameraManipulation(const glm::vec2 viewportSize,
                   Gesture& gesture,
                   CCamera& camera,
                   CameraModifier& cameraMod,
                   CameraManipulationMode mode)
{
  bool cameraEdit = false;

  if (mode == CameraManipulationMode::Planar2D) {
    const bool wheelEdit = cameraManipulationWheel(viewportSize, gesture.input, camera);

    // Planar views use direct manipulation: left-drag pans and right-drag zooms.
    // Rotation gestures are intentionally disabled.
    if (gesture.input.hasButtonAction(Gesture::Input::kButtonLeft, 0)) {
      cameraEdit =
        cameraManipulationTrack(viewportSize, gesture.input.mbs[Gesture::Input::kButtonLeft], camera, cameraMod);
    } else if (gesture.input.hasButtonAction(Gesture::Input::kButtonRight, 0)) {
      cameraEdit = cameraManipulationDolly(viewportSize,
                                           gesture.input.mbs[Gesture::Input::kButtonRight],
                                           camera,
                                           cameraMod,
                                           false,
                                           true,
                                           100.0f,
                                           1.0f);
    }

    return cameraEdit || wheelEdit;
  }

  // Camera Track
  // middle-drag or alt-left-drag (option-left-drag on Mac)
  if (gesture.input.hasButtonAction(Gesture::Input::kButtonMiddle, 0)) {
    cameraEdit =
      cameraManipulationTrack(viewportSize, gesture.input.mbs[Gesture::Input::kButtonMiddle], camera, cameraMod);
  } else if (gesture.input.hasButtonAction(Gesture::Input::kButtonLeft, Gesture::Input::kAlt)) {
    cameraEdit =
      cameraManipulationTrack(viewportSize, gesture.input.mbs[Gesture::Input::kButtonLeft], camera, cameraMod);
  }

  // Camera Roll
  else if (gesture.input.hasButtonAction(Gesture::Input::kButtonMiddle, Gesture::Input::kCtrl)) {
    cameraEdit = true;
    Gesture::Input::Button& button = gesture.input.mbs[Gesture::Input::kButtonMiddle];

    static const int ROLL_PIXELS_PER_RADIAN = 400;
    const float dragScale = -1.0f / ROLL_PIXELS_PER_RADIAN;

    glm::vec2 drag = button.drag * dragScale;
    glm::vec3 v = glm::normalize(camera.m_From - camera.m_Target);

    if (drag.x == 0 && drag.y == 0)
      cameraEdit = false;

    // Rotate the up vector around the camera to target direction
    glm::vec3 rotated_up =
      glm::rotate(camera.m_Up, drag.x, v); // xfmVector(Quaternion3f::rotate(v, drag.x), camera.up);

    if (button.action == Gesture::Input::kPress || button.action == Gesture::Input::kDrag) {
      cameraMod.up = rotated_up - camera.m_Up;
    } else if (button.action == Gesture::Input::kRelease) {
      camera.m_Up = rotated_up;
      cameraMod.position = glm::vec3(0);

      // Consume pointer button release event
      Gesture::Input::reset(button);
    }
  }

  // Camera Dolly
  // right-drag or ctrl-left-drag (command-left-drag on Mac)
  else if (gesture.input.hasButtonAction(Gesture::Input::kButtonRight, 0)) {
    cameraEdit =
      cameraManipulationDolly(viewportSize, gesture.input.mbs[Gesture::Input::kButtonRight], camera, cameraMod);
  } else if (gesture.input.hasButtonAction(Gesture::Input::kButtonLeft, Gesture::Input::kCtrl)) {
    cameraEdit =
      cameraManipulationDolly(viewportSize, gesture.input.mbs[Gesture::Input::kButtonLeft], camera, cameraMod);
  }
  // Camera Tumble
  // hasButtonAction with zero actions must be last to check for, since it will match anything
  else if (gesture.input.hasButtonAction(Gesture::Input::kButtonLeft, 0 /*Gesture::Input::kAlt*/)) {
    cameraEdit = true;
    Gesture::Input::Button& button = gesture.input.mbs[Gesture::Input::kButtonLeft];

    static const int TUMBLE_PIXELS_PER_RADIAN = 800;
    const float dragScale = 1.0f / TUMBLE_PIXELS_PER_RADIAN;

    glm::vec2 drag = button.drag * dragScale;
    if (button.dragConstraint == Gesture::Input::kHorizontal) {
      drag.y = 0;
    }
    if (button.dragConstraint == Gesture::Input::kVertical) {
      drag.x = 0;
    }

    if (drag.x == 0 && drag.y == 0) {
      cameraEdit = false;
    }

    glm::vec3 v = camera.m_From - camera.m_Target;
    glm::quat q = trackball(drag.x, -drag.y, v, camera.m_Up, camera.m_U);
    glm::vec3 rotated_up = q * camera.m_Up;
    glm::vec3 rotated_v = q * v;

    if (button.action == Gesture::Input::kPress || button.action == Gesture::Input::kDrag) {
      cameraMod.position = rotated_v - v;
      cameraMod.up = rotated_up - camera.m_Up;
    } else if (button.action == Gesture::Input::kRelease) {
      camera.m_From += rotated_v - v;
      camera.m_Up = rotated_up;
      camera.Update();

      // Consume gesture on button release
      Gesture::Input::reset(button);
    }
  }

  return cameraEdit;
}

glm::mat3
CCamera::reconstructBasis(const glm::mat3& capturedBasis) const
{
  const glm::vec3 capturedU = capturedBasis[0];
  const glm::vec3 capturedN = capturedBasis[2];

  const glm::vec3 worldU = capturedU.x * m_U + capturedU.y * m_V + capturedU.z * m_N;
  const glm::vec3 worldN = capturedN.x * m_U + capturedN.y * m_V + capturedN.z * m_N;

  return buildOrthonormalFrame(worldN, worldU);
};

glm::mat3
CCamera::captureRelativeBasis(const glm::mat3& worldBasis) const
{
  glm::vec3 worldU = worldBasis[0];
  glm::vec3 worldV = worldBasis[1];
  glm::vec3 worldN = worldBasis[2];

  glm::vec3 capturedU(glm::dot(worldU, m_U), glm::dot(worldU, m_V), glm::dot(worldU, m_N));
  glm::vec3 capturedV(glm::dot(worldV, m_U), glm::dot(worldV, m_V), glm::dot(worldV, m_N));
  glm::vec3 capturedN(glm::dot(worldN, m_U), glm::dot(worldN, m_V), glm::dot(worldN, m_N));

  return glm::mat3(capturedU, capturedV, capturedN);
};
