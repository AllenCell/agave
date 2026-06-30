#pragma once

#include "CCamera.h"
#include "Manipulator.h"
#include "Timing.h"
#include "gfxapi/Framebuffer.h"
#include "gfxapi/IGestureRenderer.h"
#include "gfxapi/IRenderWindow.h"
#include "gesture/gesture.h"

#include <memory>
#include <vector>

class RenderSettings;

class ViewerWindow
{
public:
  ViewerWindow(RenderSettings* rs);
  ~ViewerWindow();

  void setSize(int width, int height);
  int width() const { return sceneView.viewport.region.upper.x - sceneView.viewport.region.lower.x; }
  int height() const { return sceneView.viewport.region.upper.y - sceneView.viewport.region.lower.y; }

  void redraw();
  void redrawTo(gfxApi::Framebuffer* framebuffer);

  void update(const SceneView::Viewport& viewport, const Clock& clock, Gesture& gesture);

  void setRenderer(int rendererType);

  // Provide a new active tool
  void setTool(ManipulationTool* tool)
  {
    if (m_activeTool != &m_defaultTool) {
      ManipulationTool::destroyTool(m_activeTool);
    }
    m_activeTool = (tool ? tool : &m_defaultTool);

    // clear out the buffer once.
    if (!tool) {
      m_gestureRenderer->clearSelectionBuffer();
    }

    // Todo: this could be replaced with a push/pop mechanism to allow
    //       the completion of a tool to restore a previous state.
  }

  // A convenient template helper function to run all the instantiated
  // manipulator tools trough some computation (an arbitrary lambda function)
  template<typename Fn>
  void forEachTool(Fn fn)
  {
    for (ManipulationTool* tool : m_tools) {
      fn(tool);
    }
    fn(m_activeTool);
  }

  void select(SceneObject* obj);

  void updateCamera();

  // Bracket instant camera changes (toolbar view buttons, etc.) to maintain
  // light-lock-to-camera invariant. Call beginCameraChange() before modifying
  // m_CCamera, and endCameraChange() after.
  void beginCameraChange();
  void endCameraChange();

  CCamera m_CCamera;
  std::vector<CameraAnimation> m_cameraAnim;

  Gesture gesture;
  SceneView sceneView;
  Clock m_clock;
  double m_lastTimeCheck;
  double m_frameRate;
  int m_increments;

  std::unique_ptr<gfxApi::IGestureRenderer> m_gestureRenderer;

  ManipulationTool m_defaultTool; //< a null tool representing selection
  ManipulationTool* m_activeTool = &m_defaultTool;
  std::vector<ManipulationTool*> m_tools;
  bool m_toolsUseLocalSpace = false;

  RenderSettings* m_renderSettings;
  std::unique_ptr<gfxApi::IRenderWindow> m_renderer;
  int m_rendererType{ 1 };

  // Track camera manipulation for light following
  bool m_wasCameraBeingEdited = false;
};
