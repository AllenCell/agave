#include "ViewerWindow.h"

#include "AppScene.h"
#include "AxisHelperTool.h"
#include "BoundingBoxTool.h"
#include "Light.h"
#include "Logging.h"
#include "MoveTool.h"
#include "RenderSettings.h"
#include "RotateTool.h"
#include "ScaleBarTool.h"
#include "SceneLight.h"
#include "TimeStampTool.h"
#include "gfxapi/Backend.h"
#include "gfxapi/RenderToFramebuffer.h"
#include "renderlib.h"

#include <algorithm>
#include <memory>

namespace {

constexpr float MIN_SLICE_CAMERA_SCALE = 0.000001f;

gfxApi::ClearColor
backgroundClearColor(const Scene* scene)
{
  if (!scene) {
    return {};
  }

  return { scene->m_material.m_backgroundColor[0],
           scene->m_material.m_backgroundColor[1],
           scene->m_material.m_backgroundColor[2],
           0.0f };
}

float
sliceFitScale(const CCamera& camera, const CBoundingBox& bounds)
{
  if (camera.m_Film.GetWidth() <= 0 || camera.m_Film.GetHeight() <= 0) {
    return MIN_SLICE_CAMERA_SCALE;
  }

  const glm::vec3 extent = bounds.GetExtent();
  const float planeWidth = glm::dot(glm::abs(camera.m_U), extent);
  const float planeHeight = glm::dot(glm::abs(camera.m_V), extent);
  const float aspect = static_cast<float>(camera.m_Film.GetWidth()) / camera.m_Film.GetHeight();
  return glm::max(0.5f * glm::max(planeHeight, planeWidth / aspect), MIN_SLICE_CAMERA_SCALE);
}

} // namespace

ViewerWindow::ViewerWindow(RenderSettings* rs)
  : m_renderSettings(rs)
  , m_renderer(renderlib::graphicsBackend()->createRenderWindow(gfxApi::RenderWindowKind::PathTrace, rs))
  , m_gestureRenderer(renderlib::graphicsBackend()->createGestureRenderer())
{
  gesture.input.reset();

  m_tools.push_back(new ScaleBarTool());
  m_tools.push_back(new AxisHelperTool());
  m_tools.push_back(new BoundingBoxTool());
  m_tools.push_back(new TimeStampTool());
}

ViewerWindow::~ViewerWindow()
{
  // any tool associated with a selected object should be deleted via the SceneObject, not here.
  select(nullptr);

  if (m_activeTool != &m_defaultTool) {
    ManipulationTool::destroyTool(m_activeTool);
  }
  for (ManipulationTool* tool : m_tools) {
    ManipulationTool::destroyTool(tool);
  }
  m_activeTool = nullptr;
}

void
ViewerWindow::setSize(int width, int height)
{
  sceneView.viewport.region.lower.x = 0;
  sceneView.viewport.region.lower.y = 0;
  sceneView.viewport.region.upper.x = width;
  sceneView.viewport.region.upper.y = height;

  m_CCamera.m_Film.m_Resolution.SetResX(width);
  m_CCamera.m_Film.m_Resolution.SetResY(height);
  for (CCamera& camera : m_sliceCameras) {
    camera.m_Film.m_Resolution.SetResX(width);
    camera.m_Film.m_Resolution.SetResY(height);
  }

  // TODO do whatever resizing now?
}

void
ViewerWindow::select(SceneObject* obj)
{
  // if obj is currently selected then do nothing?
  if (sceneView.getSelectedObject() == obj) {
    return;
  }

  // deselect the current selection
  if (sceneView.getSelectedObject()) {
    ManipulationTool* tool = sceneView.getSelectedObject()->getSelectedTool();
    if (tool) {
      // remove moves tool to end, then erase removes from the new tool position to end
      m_tools.erase(std::remove(m_tools.begin(), m_tools.end(), tool), m_tools.end());
    }
    sceneView.getSelectedObject()->onSelection(false);
  }

  sceneView.setSelectedObject(obj);

  if (obj) {
    obj->onSelection(true);
    ManipulationTool* tool = sceneView.getSelectedObject()->getSelectedTool();
    if (tool) {
      m_tools.push_back(tool);
    }
  }
}

void
ViewerWindow::updateCamera(CameraManipulationMode mode)
{
  CCamera& camera = activeCamera();
  // Use gesture strokes (if any) to move the camera. If camera edit is still in progress, we are not
  // going to change the camera directly, instead we fill a CameraModifier object with the delta.
  CameraModifier cameraMod;
  bool cameraEdit = cameraManipulation(glm::vec2(width(), height()),
                                       // m_clock,
                                       gesture,
                                       camera,
                                       cameraMod,
                                       mode);
  // Apply camera animation transitions if we have any
  if (mode == CameraManipulationMode::Orbit3D && !m_cameraAnim.empty()) {
    for (auto it = m_cameraAnim.begin(); it != m_cameraAnim.end();) {
      CameraAnimation& anim = *it;
      anim.time += static_cast<float>(m_clock.timeIncrement);

      if (anim.time < anim.duration) { // alpha < 1.0) {
        float alpha = glm::smoothstep(0.0f, 1.0f, glm::clamp(anim.time / anim.duration, 0.0f, 1.0f));
        // Animation in-betweens are accumulated to the camera modifier
        cameraMod = cameraMod + (anim.mod * alpha);
        cameraEdit = true;
        ++it;
      } else {
        // Completed animation is applied to the camera instead
        camera = camera + anim.mod;
        it = m_cameraAnim.erase(it);
      }

      // let renderer know camera is dirty
      m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
    }
  }

  // Produce the render camera for current frame
  // m_CCamera.Update();
  CCamera renderCamera = camera;
  if (cameraEdit) {
    renderCamera = camera + cameraMod;
  }
  // renderCamera.Update();
  if (cameraEdit) {
    // let renderer know camera is dirty
    m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
  }

  sceneView.camera = renderCamera;
  sceneView.camera.Update();

  // Update lights to maintain fixed direction relative to camera view when enabled.
  if (mode == CameraManipulationMode::Orbit3D && sceneView.scene && sceneView.scene->m_lighting.lockToCamera) {
    if (cameraEdit) {
      // If we just started editing the camera, we need to capture the current view-space
      // basis from the pre-transformed camera (m_CCamera) for the lights to
      // maintain their orientation relative to the camera view during the edit.
      if (!m_wasCameraBeingEdited) {
        sceneView.scene->m_lighting.captureLightsViewSpaceBasis(camera);
      }
      // Restore the lights' view-space basis on the post-transformed camera
      sceneView.scene->m_lighting.restoreLightsViewSpaceBasis(sceneView.camera, m_renderSettings);
    }
  }

  // Track camera edit state for next frame
  m_wasCameraBeingEdited = mode == CameraManipulationMode::Orbit3D && cameraEdit;
}

void
ViewerWindow::beginCameraChange()
{
  if (sceneView.scene && sceneView.scene->m_lighting.lockToCamera) {
    sceneView.scene->m_lighting.captureLightsViewSpaceBasis(m_CCamera);
  }
}

void
ViewerWindow::endCameraChange()
{
  m_CCamera.Update();
  sceneView.camera = m_CCamera;
  sceneView.camera.Update();
  if (sceneView.scene && sceneView.scene->m_lighting.lockToCamera) {
    sceneView.scene->m_lighting.restoreLightsViewSpaceBasis(sceneView.camera, m_renderSettings);
  }
}

void
ViewerWindow::update(const SceneView::Viewport& viewport, const Clock& clock, Gesture& gesture)
{
  // [...]

  if (isSliceMode()) {
    ensureSliceCamerasCurrent();
  }

  // Each view mode owns a distinct interaction set. Triple-slice mode exposes
  // only its screen-space crosshair tool, single-slice modes use only planar
  // camera navigation, and 3D modes retain the scene/object tools.
  std::vector<ManipulationTool*> frameTools;
  if (isTripleSliceMode()) {
    frameTools.push_back(&m_sliceCrosshairTool);
  } else if (!isSliceMode()) {
    // TODO FIXME: scene-owned tools should eventually be registered directly
    // instead of collected here each frame.
    if (sceneView.scene && sceneView.scene->m_clipPlane && sceneView.scene->m_clipPlane->m_enabled &&
        sceneView.scene->m_clipPlane->getTool()) {
      frameTools.push_back(sceneView.scene->m_clipPlane->getTool());
    }
    forEachTool([&](ManipulationTool* tool) { frameTools.push_back(tool); });
  }

  for (ManipulationTool* tool : frameTools) {
    tool->clear();
  }

  // Query Gesture::Graphics for selection codes
  // If we are in mid-gesture, then we can continue to use the retained selection code.
  // if we never had anything to draw into the pick buffer, then we didn't pick anything.
  // This is a slight oversimplification because it checks for any single-button release
  // or drag: two-button drag gestures are not handled well.
  bool pickedAnything = false;
  // Triple-slice crosshairs use CPU hit testing because their configurable
  // grab radius is wider than their visible line geometry and the hit result
  // distinguishes horizontal, vertical, and intersection drags.
  if (!frameTools.empty() && !isTripleSliceMode()) {
    if (gesture.input.clickEnded() || gesture.input.isDragging()) {
      pickedAnything = gesture.graphics.m_retainedSelectionCode != Gesture::Graphics::k_noSelectionCode;
    } else {
      pickedAnything = m_gestureRenderer->pick(gesture.input, viewport, gesture.graphics.m_retainedSelectionCode);
    }
  }

  if (pickedAnything) {
    int selectionCode = gesture.graphics.getCurrentSelectionCode();
    for (ManipulationTool* tool : frameTools) {
      tool->setActiveCode(selectionCode);
    }
  } else if (isSingleSliceMode()) {
    updateCamera(CameraManipulationMode::Planar2D);
  } else if (!isTripleSliceMode()) {
    // User didn't click on a manipulator, run scene object selection
    // [...]
    updateCamera(CameraManipulationMode::Orbit3D);
  } else {
    // Triple-slice layout is fixed and intentionally has no camera navigation.
    sceneView.camera = activeCamera();
    sceneView.camera.Update();
  }

  // update sceneView.camera here?
  // It is already being updated inside of updateCamera
  // (only when camera is being manipulated!)

  // Run all manipulators and tools
  {
    // Ask manipulation tools to do something. Typically only one of them does
    // something, if anything at all
    for (ManipulationTool* tool : frameTools) {
      tool->action(sceneView, gesture);
    }

    // Leave code 0 as neutral, we can start at any number, this will not affect
    // anything else in the system... start at 1
    int selectionCodeOffset = 1;
    for (ManipulationTool* tool : frameTools) {
      tool->requestCodesRange(&selectionCodeOffset);
    }

    // Ask tools to generate draw commands
    for (ManipulationTool* tool : frameTools) {
      tool->draw(sceneView, gesture);
    }
  }

  // Manipulators may have changed the scene.
  // Update state, build BVH, etc...
  //[...]
}

void
ViewerWindow::redraw()
{
  m_clock.tick();
  // m_gesture.setTimeIncrement(m_clock.timeIncrement);
  // Display frame rate in window title
  double interval = m_clock.time - m_lastTimeCheck; //< Interval in seconds
  m_increments += 1;
  // update once per second
  if (interval >= 1.0) {
    // Compute average frame rate over the last second, if different than what we
    // display previously, update the window title.
    double newFrameRate = round(m_increments / interval);
    if (m_frameRate != newFrameRate) {
      m_frameRate = newFrameRate;

      // TODO update frame rate stats from here.
      // char title[256];
      // snprintf(title, 256, "%s | %d fps", windowTitle, frameRate);
      // glfwSetWindowTitle(mainWindow.handle, title);
    }
    m_lastTimeCheck = m_clock.time;
    m_increments = 0;
  }

  bool selectionBufferResized = !m_gestureRenderer->selectionBufferMatches(width(), height());
  bool ok = m_gestureRenderer->updateSelectionBuffer(width(), height());
  if (!ok) {
    LOG_ERROR << "Failed to update selection buffer";
  }

  // lazy init
  if (!gesture.graphics.font.isLoaded()) {
    std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    gesture.graphics.font.load(fontPath.c_str());
  }

  // renderer size may have been directly manipulated by e.g. the renderdialog
  uint32_t oldrendererwidth, oldrendererheight;
  m_renderer->getSize(oldrendererwidth, oldrendererheight);
  if (selectionBufferResized || width() != oldrendererwidth || height() != oldrendererheight) {
    m_renderer->resize(width(), height());
    setSize(width(), height());
  }

  sceneView.viewport.region = { { 0, 0 }, { width(), height() } };
  sceneView.camera = activeCamera();
  sceneView.scene = m_renderer->scene();
  sceneView.renderSettings = m_renderSettings;

  // fill gesture graphics with draw commands
  update(sceneView.viewport, m_clock, gesture);

  // ready to start drawing; clear our main framebuffer
  renderlib::graphicsBackend()->clearCurrentFramebuffer(backgroundClearColor(sceneView.scene));

  // render and then clear out draw commands from gesture graphics
  m_gestureRenderer->drawUnderlay(sceneView, gesture.graphics);

  // main scene rendering; need to blend/composite on top of overlay previously drawn
  m_renderer->render(sceneView.camera);

  // render and then clear out draw commands from gesture graphics
  m_gestureRenderer->draw(sceneView, gesture.graphics);

  // Make sure we consumed any unused input event before we poll new events.
  // (in the case of Qt we are not explicitly polling but using signals/slots.)
  gesture.input.consume();
}

void
ViewerWindow::redrawTo(gfxApi::Framebuffer* framebuffer)
{
  if (!framebuffer) {
    return;
  }

  m_clock.tick();
  // m_gesture.setTimeIncrement(m_clock.timeIncrement);
  // Display frame rate in window title
  double interval = m_clock.time - m_lastTimeCheck; //< Interval in seconds
  m_increments += 1;
  // update once per second
  if (interval >= 1.0) {
    // Compute average frame rate over the last second, if different than what we
    // display previously, update the window title.
    double newFrameRate = round(m_increments / interval);
    if (m_frameRate != newFrameRate) {
      m_frameRate = newFrameRate;

      // TODO update frame rate stats from here.
      // char title[256];
      // snprintf(title, 256, "%s | %d fps", windowTitle, frameRate);
      // glfwSetWindowTitle(mainWindow.handle, title);
    }
    m_lastTimeCheck = m_clock.time;
    m_increments = 0;
  }

  bool selectionBufferResized = !m_gestureRenderer->selectionBufferMatches(width(), height());
  bool ok = m_gestureRenderer->updateSelectionBuffer(width(), height());
  if (!ok) {
    LOG_ERROR << "Failed to update selection buffer";
  }

  // lazy init
  if (!gesture.graphics.font.isLoaded()) {
    std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    gesture.graphics.font.load(fontPath.c_str());
  }

  // renderer size may have been directly manipulated by e.g. the renderdialog
  uint32_t oldrendererwidth, oldrendererheight;
  m_renderer->getSize(oldrendererwidth, oldrendererheight);
  if (selectionBufferResized || width() != oldrendererwidth || height() != oldrendererheight) {
    m_renderer->resize(width(), height());
    setSize(width(), height());
  }

  sceneView.viewport.region = { { 0, 0 }, { width(), height() } };
  sceneView.camera = activeCamera();
  sceneView.scene = m_renderer->scene();
  sceneView.renderSettings = m_renderSettings;

  // fill gesture graphics with draw commands
  update(sceneView.viewport, m_clock, gesture);

  gfxApi::renderToFramebuffer(
    *framebuffer, *m_renderer, *m_gestureRenderer, sceneView, gesture.graphics, 0.0f);

  // Make sure we consumed any unused input event before we poll new events.
  // (in the case of Qt we are not explicitly polling but using signals/slots.)
  gesture.input.consume();
}

void
ViewerWindow::setRenderer(int rendererType)
{
  const bool wasSliceRenderer = isSliceMode();
  const bool useSliceRenderer = rendererType >= 2 && rendererType <= 5;
  Scene* sc = m_renderer ? m_renderer->scene() : nullptr;

  resetInteractionState();

  switch (rendererType) {
    case 2:
      m_renderSettings->m_SliceView.mode = SliceViewMode::Z;
      break;
    case 3:
      m_renderSettings->m_SliceView.mode = SliceViewMode::Y;
      break;
    case 4:
      m_renderSettings->m_SliceView.mode = SliceViewMode::X;
      break;
    case 5:
      m_renderSettings->m_SliceView.mode = SliceViewMode::Triple;
      break;
    default:
      break;
  }

  if (useSliceRenderer) {
    ensureSliceCamerasCurrent();
  }

  // X/Y/Z/triple are modes of one renderer. Keep its uploaded volume and
  // transfer resources alive when moving between those modes.
  if (wasSliceRenderer && useSliceRenderer && m_renderer) {
    m_rendererType = rendererType;
    m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
    return;
  }

  // clean up old renderer.
  if (m_renderer) {
    m_renderer->cleanUpResources();
  }

  switch (rendererType) {
    case 1:
      LOG_DEBUG << "Set pathtrace renderer";
      m_renderer =
        renderlib::graphicsBackend()->createRenderWindow(gfxApi::RenderWindowKind::PathTrace, m_renderSettings);
      m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      LOG_DEBUG << "Set slice renderer";
      m_renderer =
        renderlib::graphicsBackend()->createRenderWindow(gfxApi::RenderWindowKind::Slice, m_renderSettings);
      m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
      break;
    default:
      LOG_DEBUG << "Set raymarch renderer";
      m_renderer =
        renderlib::graphicsBackend()->createRenderWindow(gfxApi::RenderWindowKind::RaymarchBlended, m_renderSettings);
  };
  m_rendererType = rendererType;

  // need to update the scene in QAppearanceSettingsWidget.
  m_renderer->setScene(sc);
  m_renderer->initialize(width(), height()); // TODO , devicePixelRatioF());

  m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
ViewerWindow::resetSliceView()
{
  if (!isSingleSliceMode()) {
    return;
  }

  ensureSliceCamerasCurrent();
  if (!m_sliceCamerasInitialized) {
    return;
  }

  fitSliceCamera(activeCamera());
  if (m_renderSettings) {
    m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
  }
}

void
ViewerWindow::initializeSliceCameras(const CBoundingBox& bounds)
{
  static constexpr std::array<EViewMode, 3> VIEW_MODES = {
    ViewModeFront,
    ViewModeBottom,
    ViewModeRight,
  };

  for (std::size_t i = 0; i < m_sliceCameras.size(); ++i) {
    CCamera& camera = m_sliceCameras[i];
    camera = m_CCamera;
    camera.m_SceneBoundingBox = bounds;
    camera.SetProjectionMode(ORTHOGRAPHIC);
    camera.SetViewMode(VIEW_MODES[i]);

    // The X slice shader presents Z from left to right and Y from bottom to
    // top. ViewModeRight supplies the desired line of sight, but its default
    // up vector is Z; use Y so the camera basis matches that pane convention.
    if (i == 2) {
      camera.m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
      camera.Update();
    }

    fitSliceCamera(camera);
  }

  m_sliceCameraBounds = bounds;
  m_sliceCamerasInitialized = true;
}

void
ViewerWindow::retargetSliceCameras(const CBoundingBox& bounds)
{
  if (!m_sliceCamerasInitialized) {
    initializeSliceCameras(bounds);
    return;
  }

  const glm::vec3 oldCenter = m_sliceCameraBounds.GetCenter();
  const glm::vec3 newCenter = bounds.GetCenter();
  for (CCamera& camera : m_sliceCameras) {
    camera.Update();

    const float oldScale = glm::max(camera.m_OrthoScale, MIN_SLICE_CAMERA_SCALE);
    const float oldFitScale = sliceFitScale(camera, m_sliceCameraBounds);
    const float relativeZoom = oldFitScale / oldScale;
    const float oldPixelsPerWorld =
      static_cast<float>(camera.m_Film.GetHeight()) / (2.0f * oldScale);
    const glm::vec2 panPixels =
      glm::vec2(glm::dot(oldCenter - camera.m_Target, camera.m_U),
                glm::dot(oldCenter - camera.m_Target, camera.m_V)) *
      oldPixelsPerWorld;

    const float newScale = sliceFitScale(camera, bounds) / relativeZoom;
    const float newPixelsPerWorld =
      static_cast<float>(camera.m_Film.GetHeight()) / (2.0f * newScale);
    const glm::vec3 newTarget =
      newCenter - camera.m_U * (panPixels.x / newPixelsPerWorld) -
      camera.m_V * (panPixels.y / newPixelsPerWorld);

    const glm::vec3 reverseLineOfSight = camera.m_From - camera.m_Target;
    camera.m_From = newTarget + reverseLineOfSight * (newScale / oldScale);
    camera.m_Target = newTarget;
    camera.m_OrthoScale = newScale;
    camera.m_SceneBoundingBox = bounds;
    camera.Update();
  }

  m_sliceCameraBounds = bounds;
}

void
ViewerWindow::setSliceInteractionThreshold(float thresholdPixels)
{
  m_sliceCrosshairTool.setHitThresholdPixels(thresholdPixels);
}

SliceCrosshairHit
ViewerWindow::sliceCrosshairHit(const glm::vec2& position, float thresholdPixels) const
{
  if (!isTripleSliceMode()) {
    return SliceCrosshairHit::None;
  }

  const bool useCapturedDrag =
    gesture.input.mbs[Gesture::Input::kButtonLeft].action != Gesture::Input::kRelease;
  return m_sliceCrosshairTool.hoverHit(sceneView, position, thresholdPixels, useCapturedDrag);
}

CCamera&
ViewerWindow::activeCamera()
{
  switch (m_rendererType) {
    case 2:
      return m_sliceCameras[0];
    case 3:
      return m_sliceCameras[1];
    case 4:
      return m_sliceCameras[2];
    default:
      return m_CCamera;
  }
}

const CCamera&
ViewerWindow::activeCamera() const
{
  return const_cast<ViewerWindow*>(this)->activeCamera();
}

void
ViewerWindow::ensureSliceCamerasCurrent()
{
  Scene* scene = m_renderer ? m_renderer->scene() : nullptr;
  if (!scene || !scene->m_volume) {
    return;
  }

  const CBoundingBox& bounds = scene->m_boundingBox;
  const bool boundsChanged =
    !m_sliceCamerasInitialized || glm::any(glm::notEqual(bounds.GetMinP(), m_sliceCameraBounds.GetMinP())) ||
    glm::any(glm::notEqual(bounds.GetMaxP(), m_sliceCameraBounds.GetMaxP()));
  if (boundsChanged) {
    if (m_sliceCamerasInitialized) {
      retargetSliceCameras(bounds);
    } else {
      initializeSliceCameras(bounds);
    }
  }
}

void
ViewerWindow::fitSliceCamera(CCamera& camera)
{
  if (camera.m_Film.GetWidth() <= 0 || camera.m_Film.GetHeight() <= 0) {
    return;
  }

  const glm::vec3 reverseLineOfSight = camera.m_From - camera.m_Target;
  const float lineOfSightLength = glm::length(reverseLineOfSight);
  const glm::vec3 direction =
    lineOfSightLength > MIN_SLICE_CAMERA_SCALE ? reverseLineOfSight / lineOfSightLength : glm::vec3(0.0f, 0.0f, 1.0f);
  const float distance = glm::max(lineOfSightLength, MIN_SLICE_CAMERA_SCALE);
  camera.m_Target = camera.m_SceneBoundingBox.GetCenter();
  camera.m_From = camera.m_Target + direction * distance;
  camera.m_OrthoScale = sliceFitScale(camera, camera.m_SceneBoundingBox);
  camera.Update();
}

void
ViewerWindow::resetInteractionState()
{
  m_sliceCrosshairTool.cancelInteraction();
  m_wasCameraBeingEdited = false;
  gesture.input.reset();
  gesture.graphics.m_retainedSelectionCode = Gesture::Graphics::k_noSelectionCode;
  if (m_gestureRenderer) {
    m_gestureRenderer->clearSelectionBuffer();
  }
}
