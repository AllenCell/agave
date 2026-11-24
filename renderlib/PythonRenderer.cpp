#include "PythonRenderer.h"

#include "AppScene.h"
#include "BoundingBoxTool.h"
#include "CCamera.h"
#include "ImageXYZC.h"
#include "RenderSettings.h"
#include "ScaleBarTool.h"
#include "SceneView.h"
#include "SliceCrosshairTool.h"
#include "TimeStampTool.h"
#include "VolumeDimensions.h"
#include "VolumeInfoToJson.h"
#include "gfxapi/Backend.h"
#include "gfxapi/Framebuffer.h"
#include "gfxapi/IGestureRenderer.h"
#include "gfxapi/IRenderWindow.h"
#include "gfxapi/RenderToFramebuffer.h"
#include "gesture/gesture.h"
#include "renderlib.h"

#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

std::mutex s_runtimeMutex;
int s_runtimeUsers = 0;
std::string s_runtimeAssetPath;

template<typename T>
T
arg(const PythonRendererArguments& args, size_t index)
{
  try {
    return std::get<T>(args[index]);
  } catch (const std::bad_variant_access&) {
    throw PythonRendererArgumentError("command argument " + std::to_string(index) + " has the wrong type");
  }
}

void
requireArgs(int commandId, const PythonRendererArguments& args, size_t expected)
{
  if (args.size() != expected) {
    throw PythonRendererArgumentError("command " + std::to_string(commandId) + " expects " + std::to_string(expected) +
                                      " arguments, got " + std::to_string(args.size()));
  }
}

LoadDataCommandD
makeLoadDataCommandData(const PythonRendererArguments& args)
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
    throw PythonRendererValueError("LOAD_DATA region must be empty or contain six integers");
  }
  return data;
}

} // namespace

PythonRenderer::PythonRenderer(const std::string& mode, const std::string& assetPath, int selectedGpu)
{
  initialize(mode, assetPath, selectedGpu);
}

PythonRenderer::~PythonRenderer()
{
  close();
}

void
PythonRenderer::initialize(const std::string& mode, const std::string& assetPath, int selectedGpu)
{
  const RenderMode renderMode = parseRenderMode(mode);
  std::lock_guard<std::mutex> lock(s_runtimeMutex);
  if (s_runtimeUsers == 0) {
    gfxApi::InitParams params;
    params.backendKind = gfxApi::BackendKind::Vulkan;
    params.assetPath = assetPath;
    params.headless = true;
    params.selectedGpu = selectedGpu;
#ifndef NDEBUG
    // Enable graphics API validation/debug output in debug builds.
    params.enableDebug = true;
#endif
    if (!renderlib::initialize(params)) {
      renderlib::cleanup();
      throw std::runtime_error("Unable to initialize the headless Vulkan backend");
    }
    s_runtimeAssetPath = assetPath;
  } else {
    // Every renderer shares the one process-wide Vulkan device. Compare against
    // the GPU that was actually selected, so auto-selection matches an explicit
    // request for the same device.
    if (assetPath != s_runtimeAssetPath) {
      throw PythonRendererValueError("All Python renderers must use the same asset path");
    }
    const int runtimeGpu = renderlib::graphicsBackend()->selectedGpuIndex();
    if (selectedGpu != gfxApi::kAutoSelectGpu && selectedGpu != runtimeGpu) {
      throw PythonRendererValueError("All Python renderers share one Vulkan device; GPU " + std::to_string(runtimeGpu) +
                                     " is in use, but GPU " + std::to_string(selectedGpu) + " was requested");
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

    m_renderMode = renderMode;
    applySliceViewMode(renderMode);
    m_renderer = createVolumeRenderer(renderMode);

    auto* backend = renderlib::graphicsBackend();
    m_framebuffer = backend->createFramebuffer(
      { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), gfxApi::FramebufferColorFormat::Rgba8, true });
    m_gestureRenderer = backend->createGestureRenderer();
    m_gesture = std::make_unique<Gesture>();

    m_executionContext.m_renderer = this;
    m_executionContext.m_renderSettings = m_renderSettings.get();
    m_executionContext.m_appScene = m_scene.get();
    m_executionContext.m_camera = m_camera.get();
  } catch (...) {
    m_gesture.reset();
    m_gestureRenderer.reset();
    m_framebuffer.reset();
    m_renderer.reset();
    m_scene.reset();
    m_camera.reset();
    m_renderSettings.reset();
    if (--s_runtimeUsers == 0) {
      renderlib::cleanup();
      s_runtimeAssetPath.clear();
    }
    m_ownsRuntime = false;
    throw;
  }
}

void
PythonRenderer::close()
{
  if (m_closed) {
    return;
  }
  m_closed = true;

  if (m_renderer) {
    m_renderer->cleanUpResources();
  }
  m_gesture.reset();
  m_gestureRenderer.reset();
  m_framebuffer.reset();
  m_renderer.reset();
  m_scene.reset();
  m_camera.reset();
  m_renderSettings.reset();

  std::lock_guard<std::mutex> lock(s_runtimeMutex);
  if (m_ownsRuntime && --s_runtimeUsers == 0) {
    renderlib::cleanup();
    s_runtimeAssetPath.clear();
  }
  m_ownsRuntime = false;
}

PythonRenderer::RenderMode
PythonRenderer::parseRenderMode(const std::string& mode)
{
  if (mode == "pathtrace") {
    return RenderMode::Pathtrace;
  }
  if (mode == "raymarch") {
    return RenderMode::Raymarch;
  }
  if (mode == "slice_z") {
    return RenderMode::SliceZ;
  }
  if (mode == "slice_y") {
    return RenderMode::SliceY;
  }
  if (mode == "slice_x") {
    return RenderMode::SliceX;
  }
  if (mode == "triple") {
    return RenderMode::Triple;
  }
  throw PythonRendererValueError(
    "Render mode must be one of 'pathtrace', 'raymarch', 'slice_z', 'slice_y', 'slice_x', or 'triple'; got '" + mode +
    "'");
}

std::unique_ptr<gfxApi::IRenderWindow>
PythonRenderer::createVolumeRenderer(RenderMode mode)
{
  gfxApi::RenderWindowKind kind = gfxApi::RenderWindowKind::PathTrace;
  if (mode == RenderMode::Raymarch) {
    kind = gfxApi::RenderWindowKind::RaymarchBlended;
  } else if (isSliceMode(mode)) {
    kind = gfxApi::RenderWindowKind::Slice;
  }
  auto renderer = renderlib::graphicsBackend()->createRenderWindow(kind, m_renderSettings.get());
  if (!renderer) {
    throw std::runtime_error("Unable to create the Vulkan volume renderer");
  }
  renderer->initialize(m_width, m_height);
  renderer->setScene(m_scene.get());
  return renderer;
}

void
PythonRenderer::applySliceViewMode(RenderMode mode)
{
  SliceViewState& sliceView = m_renderSettings->m_SliceView;
  switch (mode) {
    case RenderMode::SliceZ:
      sliceView.mode = SliceViewMode::Z;
      break;
    case RenderMode::SliceY:
      sliceView.mode = SliceViewMode::Y;
      break;
    case RenderMode::SliceX:
      sliceView.mode = SliceViewMode::X;
      break;
    case RenderMode::Triple:
      sliceView.mode = SliceViewMode::Triple;
      break;
    default:
      break;
  }
}

void
PythonRenderer::syncSliceDimensions()
{
  // Volumes arrive through load_array or the load commands; re-center the
  // slices whenever the loaded volume's size changes, as the desktop app does.
  if (!m_scene || !m_scene->m_volume) {
    return;
  }
  const ImageXYZC& volume = *m_scene->m_volume;
  const glm::ivec3 dimensions(
    static_cast<int>(volume.sizeX()), static_cast<int>(volume.sizeY()), static_cast<int>(volume.sizeZ()));
  SliceViewState& sliceView = m_renderSettings->m_SliceView;
  if (sliceView.dimensions != dimensions) {
    sliceView.setDimensions(dimensions, true);
    m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
  }
}

void
PythonRenderer::setRenderMode(const std::string& mode)
{
  if (m_closed) {
    throw std::runtime_error("Renderer is closed");
  }
  const RenderMode renderMode = parseRenderMode(mode);

  // X/Y/Z/triple are modes of one slice renderer, so moving between them keeps
  // its uploaded volume, as in the desktop app.
  if (isSliceMode(m_renderMode) && isSliceMode(renderMode)) {
    m_renderMode = renderMode;
    applySliceViewMode(renderMode);
    m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
    return;
  }

  // Build the replacement first so a failure leaves the current renderer usable.
  auto renderer = createVolumeRenderer(renderMode);
  m_renderer->cleanUpResources();
  m_renderer = std::move(renderer);
  m_renderMode = renderMode;
  applySliceViewMode(renderMode);

  // The new renderer has no GPU copies of the scene yet; mark everything dirty
  // so it uploads the volume and picks up the current settings on next redraw.
  m_renderSettings->SetNoIterations(0);
  m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(VolumeDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(RoiDirty);
}

void
PythonRenderer::setSliceIndices(int32_t x, int32_t y, int32_t z)
{
  if (m_closed) {
    throw std::runtime_error("Renderer is closed");
  }
  syncSliceDimensions();
  SliceViewState& sliceView = m_renderSettings->m_SliceView;
  sliceView.indices = glm::ivec3(x, y, z);
  sliceView.clampIndices();
  m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
PythonRenderer::setStreamMode(int32_t)
{
  // Synchronous in-process rendering has no streaming mode.
}

void
PythonRenderer::resizeGL(int x, int y)
{
  m_width = x;
  m_height = y;
  m_renderer->resize(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
  m_framebuffer = renderlib::graphicsBackend()->createFramebuffer(
    { static_cast<uint32_t>(x), static_cast<uint32_t>(y), gfxApi::FramebufferColorFormat::Rgba8, true });
}

std::string
PythonRenderer::loadVolume(std::shared_ptr<ImageXYZC> image,
                           const VolumeDimensions& dimensions,
                           const std::string& name)
{
  if (m_closed) {
    throw std::runtime_error("Renderer is closed");
  }
  if (!image) {
    throw PythonRendererValueError("Volume image must not be null");
  }
  if (image->sizeC() > MAX_CPU_CHANNELS) {
    throw PythonRendererValueError("Volume contains more than 32 channels");
  }

  m_executionContext.m_loadSpec = LoadSpec{};
  m_executionContext.m_loadSpec.filepath = name;
  m_executionContext.setReader(m_executionContext.m_loadSpec, nullptr);

  m_scene->m_timeLine.setRange(0, 0);
  m_scene->m_timeLine.setCurrentTime(0);
  m_scene->m_timeLine.setTimeUnit(dimensions.timeUnit);
  m_scene->m_timeLine.setTimeUnits(dimensions.timeUnits);
  m_scene->m_volume = image;
  m_scene->initSceneFromImg(image);

  m_camera->m_SceneBoundingBox.m_MinP = m_scene->m_boundingBox.GetMinP();
  m_camera->m_SceneBoundingBox.m_MaxP = m_scene->m_boundingBox.GetMaxP();
  m_camera->SetViewMode(ViewModeFront);

  for (uint32_t channel = 0; channel < image->sizeC(); ++channel) {
    m_scene->m_material.m_enabled[channel] = channel < ImageXYZC::FIRST_N_CHANNELS;
    m_scene->m_material.m_opacity[channel] = 1.0f;
  }
  m_renderSettings->SetNoIterations(0);
  m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(VolumeDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
  m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
  syncSliceDimensions();

  return volumeInfoToJson(image, dimensions, name).dump();
}

PythonRendererResult
PythonRenderer::run(Command& command)
{
  if (m_closed) {
    throw std::runtime_error("Renderer is closed");
  }
  m_executionContext.m_message.clear();
  command.execute(&m_executionContext);
  if (!m_executionContext.m_message.empty()) {
    return m_executionContext.m_message;
  }
  return std::monostate{};
}

PythonRendererResult
PythonRenderer::run(RequestRedrawCommand& command)
{
  run(static_cast<Command&>(command));
  return redraw();
}

PythonRendererResult
PythonRenderer::redraw()
{
  m_camera->Update();
  syncSliceDimensions();

  SceneView sceneView;
  sceneView.viewport.region = { { 0, 0 }, { m_width, m_height } };
  sceneView.camera = *m_camera;
  sceneView.scene = m_scene.get();
  sceneView.renderSettings = m_renderSettings.get();

  m_gesture->graphics.clearCommands();
  if (!m_gesture->graphics.font.isLoaded()) {
    const std::string fontPath = renderlib::assetPath() + "/fonts/Arial.ttf";
    m_gesture->graphics.font.load(fontPath.c_str());
  }
  if (isSliceMode(m_renderMode)) {
    // Slice panes are drawn fitted to the viewport. The slice renderer only
    // applies pan/zoom for an orthographic camera, and the 3D camera must not
    // leak into the 2D view, so hand it a perspective copy.
    sceneView.camera.m_Projection = PERSPECTIVE;
    // Like the desktop app, slice views draw no 3D overlays; triple-slice
    // shows its plane crosshairs.
    if (m_renderMode == RenderMode::Triple) {
      SliceCrosshairTool crosshair;
      crosshair.draw(sceneView, *m_gesture);
    }
  } else {
    ScaleBarTool scaleBar;
    scaleBar.draw(sceneView, *m_gesture);
    BoundingBoxTool boundingBox;
    boundingBox.draw(sceneView, *m_gesture);
    TimeStampTool timeStamp;
    timeStamp.draw(sceneView, *m_gesture);
  }

  gfxApi::renderToFramebuffer(*m_framebuffer, *m_renderer, *m_gestureRenderer, sceneView, m_gesture->graphics, 0.0f);

  PythonRendererImage image{ m_width,
                             m_height,
                             std::vector<uint8_t>(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4) };
  m_framebuffer->toImage(image.pixels.data());
  return image;
}

std::vector<CommandArgType>
PythonRenderer::commandArgumentTypes(int commandId)
{
#define COMMAND_ARGUMENT_TYPES(ID, TYPE)                                                                               \
  case ID:                                                                                                             \
    return TYPE::ArgTypes()

  switch (commandId) {
    COMMAND_ARGUMENT_TYPES(0, SessionCommand);
    COMMAND_ARGUMENT_TYPES(1, AssetPathCommand);
    COMMAND_ARGUMENT_TYPES(2, LoadOmeTifCommand);
    COMMAND_ARGUMENT_TYPES(3, SetCameraPosCommand);
    COMMAND_ARGUMENT_TYPES(4, SetCameraTargetCommand);
    COMMAND_ARGUMENT_TYPES(5, SetCameraUpCommand);
    COMMAND_ARGUMENT_TYPES(6, SetCameraApertureCommand);
    COMMAND_ARGUMENT_TYPES(7, SetCameraProjectionCommand);
    COMMAND_ARGUMENT_TYPES(8, SetCameraFocalDistanceCommand);
    COMMAND_ARGUMENT_TYPES(9, SetCameraExposureCommand);
    COMMAND_ARGUMENT_TYPES(10, SetDiffuseColorCommand);
    COMMAND_ARGUMENT_TYPES(11, SetSpecularColorCommand);
    COMMAND_ARGUMENT_TYPES(12, SetEmissiveColorCommand);
    COMMAND_ARGUMENT_TYPES(13, SetRenderIterationsCommand);
    COMMAND_ARGUMENT_TYPES(14, SetStreamModeCommand);
    COMMAND_ARGUMENT_TYPES(15, RequestRedrawCommand);
    COMMAND_ARGUMENT_TYPES(16, SetResolutionCommand);
    COMMAND_ARGUMENT_TYPES(17, SetDensityCommand);
    COMMAND_ARGUMENT_TYPES(18, FrameSceneCommand);
    COMMAND_ARGUMENT_TYPES(19, SetGlossinessCommand);
    COMMAND_ARGUMENT_TYPES(20, EnableChannelCommand);
    COMMAND_ARGUMENT_TYPES(21, SetWindowLevelCommand);
    COMMAND_ARGUMENT_TYPES(22, OrbitCameraCommand);
    COMMAND_ARGUMENT_TYPES(23, SetSkylightTopColorCommand);
    COMMAND_ARGUMENT_TYPES(24, SetSkylightMiddleColorCommand);
    COMMAND_ARGUMENT_TYPES(25, SetSkylightBottomColorCommand);
    COMMAND_ARGUMENT_TYPES(26, SetLightPosCommand);
    COMMAND_ARGUMENT_TYPES(27, SetLightColorCommand);
    COMMAND_ARGUMENT_TYPES(28, SetLightSizeCommand);
    COMMAND_ARGUMENT_TYPES(29, SetClipRegionCommand);
    COMMAND_ARGUMENT_TYPES(30, SetVoxelScaleCommand);
    COMMAND_ARGUMENT_TYPES(31, AutoThresholdCommand);
    COMMAND_ARGUMENT_TYPES(32, SetPercentileThresholdCommand);
    COMMAND_ARGUMENT_TYPES(33, SetOpacityCommand);
    COMMAND_ARGUMENT_TYPES(34, SetPrimaryRayStepSizeCommand);
    COMMAND_ARGUMENT_TYPES(35, SetSecondaryRayStepSizeCommand);
    COMMAND_ARGUMENT_TYPES(36, SetBackgroundColorCommand);
    COMMAND_ARGUMENT_TYPES(37, SetIsovalueThresholdCommand);
    COMMAND_ARGUMENT_TYPES(38, SetControlPointsCommand);
    COMMAND_ARGUMENT_TYPES(39, LoadVolumeFromFileCommand);
    COMMAND_ARGUMENT_TYPES(40, SetTimeCommand);
    COMMAND_ARGUMENT_TYPES(41, SetBoundingBoxColorCommand);
    COMMAND_ARGUMENT_TYPES(42, ShowBoundingBoxCommand);
    COMMAND_ARGUMENT_TYPES(43, TrackballCameraCommand);
    COMMAND_ARGUMENT_TYPES(44, LoadDataCommand);
    COMMAND_ARGUMENT_TYPES(45, ShowScaleBarCommand);
    COMMAND_ARGUMENT_TYPES(46, SetFlipAxisCommand);
    COMMAND_ARGUMENT_TYPES(47, SetInterpolationCommand);
    COMMAND_ARGUMENT_TYPES(48, SetClipPlaneCommand);
    COMMAND_ARGUMENT_TYPES(49, SetColorRampCommand);
    COMMAND_ARGUMENT_TYPES(50, SetMinMaxThresholdCommand);
    COMMAND_ARGUMENT_TYPES(51, SetSkylightRotationCommand);
    COMMAND_ARGUMENT_TYPES(52, ShowTimeStampCommand);
    COMMAND_ARGUMENT_TYPES(53, SetTimeStampFormatCommand);
    COMMAND_ARGUMENT_TYPES(54, SetMultichannelBlendCommand);
    COMMAND_ARGUMENT_TYPES(55, SetWoodcockTrackingCommand);
    default:
      throw PythonRendererValueError("Unknown AGAVE command id " + std::to_string(commandId));
  }

#undef COMMAND_ARGUMENT_TYPES
}

PythonRendererResult
PythonRenderer::execute(int commandId, const PythonRendererArguments& args)
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
    EXECUTE_COMMAND(54, 1, SetMultichannelBlendCommand, SetMultichannelBlendCommandD{ arg<int32_t>(args, 0) });
    EXECUTE_COMMAND(55, 1, SetWoodcockTrackingCommand, SetWoodcockTrackingCommandD{ arg<int32_t>(args, 0) });
    default:
      throw PythonRendererValueError("Unknown AGAVE command id " + std::to_string(commandId));
  }

#undef EXECUTE_COMMAND
}
