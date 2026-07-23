#include "VulkanRenderer.h"

#include "renderlib/AppScene.h"
#include "renderlib/BoundingBoxTool.h"
#include "renderlib/CCamera.h"
#include "renderlib/RenderSettings.h"
#include "renderlib/ScaleBarTool.h"
#include "renderlib/SceneView.h"
#include "renderlib/TimeStampTool.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/gfxapi/Framebuffer.h"
#include "renderlib/gfxapi/IGestureRenderer.h"
#include "renderlib/gfxapi/IRenderWindow.h"
#include "renderlib/gfxapi/RenderToFramebuffer.h"
#include "renderlib/gesture/gesture.h"
#include "renderlib/renderlib.h"

#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nb = nanobind;

namespace {

std::mutex s_runtimeMutex;
int s_runtimeUsers = 0;

template<typename T>
T
arg(const nb::args& args, size_t index)
{
  return nb::cast<T>(args[index]);
}

void
requireArgs(int commandId, const nb::args& args, size_t expected)
{
  if (args.size() != expected) {
    throw nb::type_error(("command " + std::to_string(commandId) + " expects " + std::to_string(expected) +
                          " arguments, got " + std::to_string(args.size()))
                           .c_str());
  }
}

LoadDataCommandD
makeLoadDataCommandData(const nb::args& args)
{
  const auto region = arg<std::vector<int32_t>>(args, 5);
  LoadDataCommandD data{ arg<std::string>(args, 0),
                         arg<int32_t>(args, 1),
                         arg<int32_t>(args, 2),
                         arg<int32_t>(args, 3),
                         arg<std::vector<int32_t>>(args, 4),
                         0,
                         0,
                         0,
                         0,
                         0,
                         0 };
  if (region.size() == 6) {
    data.m_xmin = region[0];
    data.m_xmax = region[1];
    data.m_ymin = region[2];
    data.m_ymax = region[3];
    data.m_zmin = region[4];
    data.m_zmax = region[5];
  } else if (!region.empty()) {
    throw nb::value_error("LOAD_DATA region must be empty or contain six integers");
  }
  return data;
}

} // namespace

VulkanRenderer::VulkanRenderer(const std::string& mode, const std::string& assetPath, int selectedGpu)
{
  initialize(mode, assetPath, selectedGpu);
}

VulkanRenderer::~VulkanRenderer()
{
  close();
}

void
VulkanRenderer::initialize(const std::string& mode, const std::string& assetPath, int selectedGpu)
{
  std::lock_guard<std::mutex> lock(s_runtimeMutex);
  if (s_runtimeUsers == 0) {
    gfxApi::InitParams params;
    params.backendKind = gfxApi::BackendKind::Vulkan;
    params.assetPath = assetPath;
    params.headless = true;
    params.selectedGpu = selectedGpu;
    if (!renderlib::initialize(params)) {
      throw std::runtime_error("Unable to initialize the headless Vulkan backend");
    }
  }
  ++s_runtimeUsers;
  m_ownsRuntime = true;

  try {
    m_renderSettings = std::make_unique<RenderSettings>();
    m_camera = std::make_unique<CCamera>();
    m_camera->m_Film.m_ExposureIterations = 1;
    m_camera->m_Film.m_Resolution.SetResX(m_width);
    m_camera->m_Film.m_Resolution.SetResY(m_height);

    m_scene = std::make_unique<Scene>();
    m_scene->initLights();

    const auto rendererType = mode == "raymarch" ? renderlib::RendererType_Raymarch : renderlib::RendererType_Pathtrace;
    m_renderer.reset(renderlib::createRenderer(rendererType, m_renderSettings.get()));
    if (!m_renderer) {
      throw std::runtime_error("Unable to create the Vulkan volume renderer");
    }
    m_renderer->initialize(m_width, m_height);
    m_renderer->setScene(m_scene.get());

    auto* backend = renderlib::graphicsBackend();
    m_framebuffer = backend->createFramebuffer(
      { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), gfxApi::FramebufferColorFormat::Rgba8, true });
    m_gestureRenderer = backend->createGestureRenderer();

    m_executionContext.m_renderer = this;
    m_executionContext.m_renderSettings = m_renderSettings.get();
    m_executionContext.m_appScene = m_scene.get();
    m_executionContext.m_camera = m_camera.get();
  } catch (...) {
    m_gestureRenderer.reset();
    m_framebuffer.reset();
    m_renderer.reset();
    m_scene.reset();
    m_camera.reset();
    m_renderSettings.reset();
    if (--s_runtimeUsers == 0) {
      renderlib::cleanup();
    }
    m_ownsRuntime = false;
    throw;
  }
}

void
VulkanRenderer::close()
{
  if (m_closed) {
    return;
  }
  m_closed = true;

  if (m_renderer) {
    m_renderer->cleanUpResources();
  }
  m_gestureRenderer.reset();
  m_framebuffer.reset();
  m_renderer.reset();
  m_scene.reset();
  m_camera.reset();
  m_renderSettings.reset();

  std::lock_guard<std::mutex> lock(s_runtimeMutex);
  if (m_ownsRuntime && --s_runtimeUsers == 0) {
    renderlib::cleanup();
  }
  m_ownsRuntime = false;
}

void
VulkanRenderer::setStreamMode(int32_t)
{
  // Synchronous in-process rendering has no streaming mode.
}

void
VulkanRenderer::resizeGL(int x, int y)
{
  m_width = x;
  m_height = y;
  m_renderer->resize(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
  m_framebuffer = renderlib::graphicsBackend()->createFramebuffer(
    { static_cast<uint32_t>(x), static_cast<uint32_t>(y), gfxApi::FramebufferColorFormat::Rgba8, true });
}

nb::object
VulkanRenderer::run(Command& command)
{
  if (m_closed) {
    throw std::runtime_error("Renderer is closed");
  }
  m_executionContext.m_message.clear();
  command.execute(&m_executionContext);
  if (!m_executionContext.m_message.empty()) {
    return nb::str(m_executionContext.m_message.c_str());
  }
  return nb::none();
}

nb::object
VulkanRenderer::run(RequestRedrawCommand& command)
{
  run(static_cast<Command&>(command));
  return redraw();
}

nb::object
VulkanRenderer::redraw()
{
  m_camera->Update();

  SceneView sceneView;
  sceneView.viewport.region = { { 0, 0 }, { m_width, m_height } };
  sceneView.camera = *m_camera;
  sceneView.scene = m_scene.get();
  sceneView.renderSettings = m_renderSettings.get();

  Gesture gesture;
  if (!gesture.graphics.font.isLoaded()) {
    const std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    gesture.graphics.font.load(fontPath.c_str());
  }
  ScaleBarTool scaleBar;
  scaleBar.draw(sceneView, gesture);
  BoundingBoxTool boundingBox;
  boundingBox.draw(sceneView, gesture);
  TimeStampTool timeStamp;
  timeStamp.draw(sceneView, gesture);

  gfxApi::renderToFramebuffer(*m_framebuffer, *m_renderer, *m_gestureRenderer, sceneView, gesture.graphics, 0.0f);

  std::vector<uint8_t> pixels(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4);
  m_framebuffer->toImage(pixels.data());
  return nb::make_tuple(m_width, m_height, nb::bytes(reinterpret_cast<const char*>(pixels.data()), pixels.size()));
}

nb::object
VulkanRenderer::execute(int commandId, const nb::args& args)
{
#define EXECUTE_COMMAND(ID, COUNT, TYPE, ...)                                                                          \
  case ID: {                                                                                                           \
    requireArgs(ID, args, TYPE::ArgTypes().size());                                                                    \
    TYPE command(__VA_ARGS__);                                                                                         \
    return run(command);                                                                                               \
  }

  switch (commandId) {
    EXECUTE_COMMAND(0, 1, SessionCommand, SessionCommandD{ arg<std::string>(args, 0) });
    EXECUTE_COMMAND(1, 1, AssetPathCommand, AssetPathCommandD{ arg<std::string>(args, 0) });
    EXECUTE_COMMAND(2, 1, LoadOmeTifCommand, LoadOmeTifCommandD{ arg<std::string>(args, 0) });
    EXECUTE_COMMAND(
      3, 3, SetCameraPosCommand, SetCameraPosCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(4,
                    3,
                    SetCameraTargetCommand,
                    SetCameraTargetCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(
      5, 3, SetCameraUpCommand, SetCameraUpCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(6, 1, SetCameraApertureCommand, SetCameraApertureCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(
      7, 2, SetCameraProjectionCommand, SetCameraProjectionCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1) });
    EXECUTE_COMMAND(8, 1, SetCameraFocalDistanceCommand, SetCameraFocalDistanceCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(9, 1, SetCameraExposureCommand, SetCameraExposureCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(
      10,
      5,
      SetDiffuseColorCommand,
      SetDiffuseColorCommandD{
        arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3), arg<float>(args, 4) });
    EXECUTE_COMMAND(
      11,
      5,
      SetSpecularColorCommand,
      SetSpecularColorCommandD{
        arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3), arg<float>(args, 4) });
    EXECUTE_COMMAND(
      12,
      5,
      SetEmissiveColorCommand,
      SetEmissiveColorCommandD{
        arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3), arg<float>(args, 4) });
    EXECUTE_COMMAND(13, 1, SetRenderIterationsCommand, SetRenderIterationsCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(14, 1, SetStreamModeCommand, SetStreamModeCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(15, 0, RequestRedrawCommand, RequestRedrawCommandD{});
    EXECUTE_COMMAND(16, 2, SetResolutionCommand, SetResolutionCommandD{ arg<int32_t>(args, 0), arg<int32_t>(args, 1) });
    EXECUTE_COMMAND(17, 1, SetDensityCommand, SetDensityCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(18, 0, FrameSceneCommand, FrameSceneCommandD{});
    EXECUTE_COMMAND(19, 2, SetGlossinessCommand, SetGlossinessCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1) });
    EXECUTE_COMMAND(20, 2, EnableChannelCommand, EnableChannelCommandD{ arg<int32_t>(args, 0), arg<int32_t>(args, 1) });
    EXECUTE_COMMAND(21,
                    3,
                    SetWindowLevelCommand,
                    SetWindowLevelCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(22, 2, OrbitCameraCommand, OrbitCameraCommandD{ arg<float>(args, 0), arg<float>(args, 1) });
    EXECUTE_COMMAND(23,
                    3,
                    SetSkylightTopColorCommand,
                    SetSkylightTopColorCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(24,
                    3,
                    SetSkylightMiddleColorCommand,
                    SetSkylightMiddleColorCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(25,
                    3,
                    SetSkylightBottomColorCommand,
                    SetSkylightBottomColorCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(
      26,
      4,
      SetLightPosCommand,
      SetLightPosCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3) });
    EXECUTE_COMMAND(
      27,
      4,
      SetLightColorCommand,
      SetLightColorCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3) });
    EXECUTE_COMMAND(28,
                    3,
                    SetLightSizeCommand,
                    SetLightSizeCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(29,
                    6,
                    SetClipRegionCommand,
                    SetClipRegionCommandD{ arg<float>(args, 0),
                                           arg<float>(args, 1),
                                           arg<float>(args, 2),
                                           arg<float>(args, 3),
                                           arg<float>(args, 4),
                                           arg<float>(args, 5) });
    EXECUTE_COMMAND(30,
                    3,
                    SetVoxelScaleCommand,
                    SetVoxelScaleCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(31, 2, AutoThresholdCommand, AutoThresholdCommandD{ arg<int32_t>(args, 0), arg<int32_t>(args, 1) });
    EXECUTE_COMMAND(32,
                    3,
                    SetPercentileThresholdCommand,
                    SetPercentileThresholdCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(33, 2, SetOpacityCommand, SetOpacityCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1) });
    EXECUTE_COMMAND(34, 1, SetPrimaryRayStepSizeCommand, SetPrimaryRayStepSizeCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(35, 1, SetSecondaryRayStepSizeCommand, SetSecondaryRayStepSizeCommandD{ arg<float>(args, 0) });
    EXECUTE_COMMAND(36,
                    3,
                    SetBackgroundColorCommand,
                    SetBackgroundColorCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(37,
                    3,
                    SetIsovalueThresholdCommand,
                    SetIsovalueThresholdCommandD{ arg<int32_t>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(38,
                    2,
                    SetControlPointsCommand,
                    SetControlPointsCommandD{ arg<int32_t>(args, 0), arg<std::vector<float>>(args, 1) });
    EXECUTE_COMMAND(
      39,
      3,
      LoadVolumeFromFileCommand,
      LoadVolumeFromFileCommandD{ arg<std::string>(args, 0), arg<int32_t>(args, 1), arg<int32_t>(args, 2) });
    EXECUTE_COMMAND(40, 1, SetTimeCommand, SetTimeCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(41,
                    3,
                    SetBoundingBoxColorCommand,
                    SetBoundingBoxColorCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2) });
    EXECUTE_COMMAND(42, 1, ShowBoundingBoxCommand, ShowBoundingBoxCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(43, 2, TrackballCameraCommand, TrackballCameraCommandD{ arg<float>(args, 0), arg<float>(args, 1) });
    EXECUTE_COMMAND(44, 6, LoadDataCommand, makeLoadDataCommandData(args));
    EXECUTE_COMMAND(45, 1, ShowScaleBarCommand, ShowScaleBarCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(46,
                    3,
                    SetFlipAxisCommand,
                    SetFlipAxisCommandD{ arg<int32_t>(args, 0), arg<int32_t>(args, 1), arg<int32_t>(args, 2) });
    EXECUTE_COMMAND(47, 1, SetInterpolationCommand, SetInterpolationCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(
      48,
      4,
      SetClipPlaneCommand,
      SetClipPlaneCommandD{ arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3) });
    EXECUTE_COMMAND(
      49,
      3,
      SetColorRampCommand,
      SetColorRampCommandD{ arg<int32_t>(args, 0), arg<std::string>(args, 1), arg<std::vector<float>>(args, 2) });
    EXECUTE_COMMAND(50,
                    3,
                    SetMinMaxThresholdCommand,
                    SetMinMaxThresholdCommandD{ arg<int32_t>(args, 0), arg<int32_t>(args, 1), arg<int32_t>(args, 2) });
    EXECUTE_COMMAND(51,
                    4,
                    SetSkylightRotationCommand,
                    SetSkylightRotationCommandD{
                      arg<float>(args, 0), arg<float>(args, 1), arg<float>(args, 2), arg<float>(args, 3) });
    EXECUTE_COMMAND(52, 1, ShowTimeStampCommand, ShowTimeStampCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(53, 1, SetTimeStampFormatCommand, SetTimeStampFormatCommandD{ arg<int32_t>(args, 0) });
    default:
      throw nb::value_error(("Unknown AGAVE command id " + std::to_string(commandId)).c_str());
  }

#undef EXECUTE_COMMAND
}
