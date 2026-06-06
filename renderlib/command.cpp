#include "command.h"

#include "AppScene.h"
#include "CCamera.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "RenderSettings.h"
#include "StringUtil.h"
#include "VolumeDimensions.h"

#include "json/json.hpp"

#include <errno.h>
#include <sys/stat.h>

#include <regex>

#if defined(WIN32)
#define STAT64_STRUCT __stat64
#define STAT64_FUNCTION _stat64
#elif defined(__APPLE__)
#define STAT64_STRUCT stat
#define STAT64_FUNCTION stat
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define STAT64_STRUCT stat
#define STAT64_FUNCTION stat
#else
#define STAT64_STRUCT stat64
#define STAT64_FUNCTION stat64
#endif

void
SessionCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "Session command: " << m_data.m_name;
}

void
AssetPathCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "AssetPath command: " << m_data.m_name;
}

void
LoadOmeTifCommand::execute(ExecutionContext* c)
{
  LOG_WARNING << "LoadOmeTif command is deprecated. Prefer LoadData command.";
  LOG_DEBUG << "LoadOmeTif command: " << m_data.m_name;
  struct STAT64_STRUCT buf;
  if (STAT64_FUNCTION(m_data.m_name.c_str(), &buf) == 0) {
    LoadSpec loadSpec;
    loadSpec.filepath = m_data.m_name;
    loadSpec.scene = 0;
    loadSpec.time = 0;
    std::unique_ptr<IFileReader> reader(FileReader::getReader(loadSpec.filepath));
    if (!reader) {
      LOG_ERROR << "Could not find a reader for file " << loadSpec.filepath;
      return;
    }

    std::shared_ptr<ImageXYZC> image = reader->loadFromFile(loadSpec);
    if (!image) {
      return;
    }

    c->m_loadSpec = loadSpec;

    c->m_appScene->m_volume = image;
    c->m_appScene->initSceneFromImg(image);

    // Tell the camera about the volume's bounding box
    c->m_camera->m_SceneBoundingBox.m_MinP = c->m_appScene->m_boundingBox.GetMinP();
    c->m_camera->m_SceneBoundingBox.m_MaxP = c->m_appScene->m_boundingBox.GetMaxP();
    c->m_camera->SetViewMode(ViewModeFront);

    // enable initial channels
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      c->m_appScene->m_material.m_enabled[i] = (i < ImageXYZC::FIRST_N_CHANNELS);
      c->m_appScene->m_material.m_opacity[i] = 1.0f;
    }
    c->m_renderSettings->SetNoIterations(0);
    c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
    // fire back some json immediately...
    nlohmann::json j;
    j["commandId"] = (int)LoadOmeTifCommand::m_ID;
    j["x"] = (int)image->sizeX();
    j["y"] = (int)image->sizeY();
    j["z"] = (int)image->sizeZ();
    j["c"] = (int)image->sizeC();
    j["t"] = 1;
    j["pixel_size_x"] = image->physicalSizeX();
    j["pixel_size_y"] = image->physicalSizeY();
    j["pixel_size_z"] = image->physicalSizeZ();
    std::vector<std::string> channelNames;
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      channelNames.push_back((image->channel(i)->m_name));
    }
    j["channel_names"] = channelNames;
    std::vector<uint16_t> channelMaxIntensity;
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      channelMaxIntensity.push_back(image->channel(i)->m_histogram.getDataMax());
    }
    j["channel_max_intensity"] = channelMaxIntensity;

    c->m_message = j.dump();
  } else {
    LOG_WARNING << "stat failed on image with errno " << errno;
  }
}

void
SetCameraPosCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraPos " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z;
  c->m_camera->m_From.x = m_data.m_x;
  c->m_camera->m_From.y = m_data.m_y;
  c->m_camera->m_From.z = m_data.m_z;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}

void
SetCameraTargetCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraTarget " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z;
  c->m_camera->m_Target.x = m_data.m_x;
  c->m_camera->m_Target.y = m_data.m_y;
  c->m_camera->m_Target.z = m_data.m_z;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}

void
SetCameraUpCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraUp " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z;
  c->m_camera->m_Up.x = m_data.m_x;
  c->m_camera->m_Up.y = m_data.m_y;
  c->m_camera->m_Up.z = m_data.m_z;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}

void
SetCameraApertureCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraAperture " << m_data.m_x;
  c->m_camera->m_Aperture.m_Size = m_data.m_x;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetCameraProjectionCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraProjection " << m_data.m_projectionType << " " << m_data.m_x;
  // We expect to get a 0 for PERSPECTIVE, or 1 for ORTHOGRAPHIC
  // Anything else will be treated as PERSPECTIVE
  c->m_camera->SetProjectionMode(m_data.m_projectionType == 1 ? ORTHOGRAPHIC : PERSPECTIVE);
  if (c->m_camera->m_Projection == ORTHOGRAPHIC) {
    c->m_camera->m_OrthoScale = m_data.m_x;
  } else {
    c->m_camera->m_FovV = m_data.m_x;
  }
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetCameraFocalDistanceCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraFocalDistance " << m_data.m_x;

  // TODO: how will we ever set the camera back to auto focus?
  c->m_camera->m_Focus.m_Type = Focus::Manual;

  c->m_camera->m_Focus.m_FocalDistance = m_data.m_x;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetCameraExposureCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetCameraExposure " << m_data.m_x;
  // 0 is darkness, 1 is max
  c->m_camera->m_Film.m_Exposure = 1.0f - m_data.m_x;
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetDiffuseColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetDiffuse " << m_data.m_channel << " " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->m_material.m_diffuse[m_data.m_channel * 3 + 0] = m_data.m_r;
  c->m_appScene->m_material.m_diffuse[m_data.m_channel * 3 + 1] = m_data.m_g;
  c->m_appScene->m_material.m_diffuse[m_data.m_channel * 3 + 2] = m_data.m_b;
  c->m_renderSettings->SetNoIterations(0);
}
void
SetSpecularColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSpecular " << m_data.m_channel << " " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->m_material.m_specular[m_data.m_channel * 3 + 0] = m_data.m_r;
  c->m_appScene->m_material.m_specular[m_data.m_channel * 3 + 1] = m_data.m_g;
  c->m_appScene->m_material.m_specular[m_data.m_channel * 3 + 2] = m_data.m_b;
  c->m_renderSettings->SetNoIterations(0);
}
void
SetEmissiveColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetEmissive " << m_data.m_channel << " " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->m_material.m_emissive[m_data.m_channel * 3 + 0] = m_data.m_r;
  c->m_appScene->m_material.m_emissive[m_data.m_channel * 3 + 1] = m_data.m_g;
  c->m_appScene->m_material.m_emissive[m_data.m_channel * 3 + 2] = m_data.m_b;
  c->m_renderSettings->SetNoIterations(0);
}
void
SetRenderIterationsCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetRenderIterations " << m_data.m_x;
  c->m_camera->m_Film.m_ExposureIterations = m_data.m_x;
}
void
SetStreamModeCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetStreamMode " << m_data.m_x;
  if (c->m_renderer) {
    c->m_renderer->setStreamMode(m_data.m_x);
  }
}
void
RequestRedrawCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "RequestRedraw";
  //	c->_renderer->renderNow();
}
void
SetResolutionCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetResolution " << m_data.m_x << " " << m_data.m_y;
  if (m_data.m_x == 0 || m_data.m_y == 0) {
    LOG_ERROR << "Invalid resolution: " << m_data.m_x << ", " << m_data.m_y;
  }
  int32_t x = std::max(m_data.m_x, 2);
  int32_t y = std::max(m_data.m_y, 2);
  c->m_camera->m_Film.m_Resolution.SetResX(x);
  c->m_camera->m_Film.m_Resolution.SetResY(y);
  if (c->m_renderer) {
    c->m_renderer->resizeGL(x, y);
  }
  c->m_renderSettings->SetNoIterations(0);
}
void
SetDensityCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetDensity " << m_data.m_x;
  c->m_renderSettings->m_RenderSettings.m_DensityScale = m_data.m_x;
  c->m_renderSettings->SetNoIterations(0);
}

void
FrameSceneCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "FrameScene";
  c->m_camera->m_SceneBoundingBox.m_MinP = c->m_appScene->m_boundingBox.GetMinP();
  c->m_camera->m_SceneBoundingBox.m_MaxP = c->m_appScene->m_boundingBox.GetMaxP();
  c->m_camera->SetViewMode(ViewModeFront);
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetGlossinessCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetGlossiness " << m_data.m_channel << " " << m_data.m_glossiness;
  c->m_appScene->m_material.m_roughness[m_data.m_channel] = m_data.m_glossiness;
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}
void
EnableChannelCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "EnableChannel " << m_data.m_channel << " " << m_data.m_enabled;
  // 0 or 1 hopefully.
  c->m_appScene->m_material.m_enabled[m_data.m_channel] = (m_data.m_enabled != 0);
  c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty | TransferFunctionDirty);
}
void
SetWindowLevelCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetWindowLevel " << m_data.m_channel << " " << m_data.m_window << " " << m_data.m_level;
  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];
  lutInfo.m_activeMode = GradientEditMode::WINDOW_LEVEL;
  lutInfo.m_window = m_data.m_window;
  lutInfo.m_level = m_data.m_level;
  c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}
void
OrbitCameraCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "OrbitCamera " << m_data.m_theta << " " << m_data.m_phi;
  c->m_camera->Orbit(m_data.m_theta, m_data.m_phi);
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
SetSkylightTopColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSkylightTopColor " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->SphereLight().m_ColorTop = glm::vec3(m_data.m_r, m_data.m_g, m_data.m_b);
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetSkylightMiddleColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSkylightMiddleColor " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->SphereLight().m_ColorMiddle = glm::vec3(m_data.m_r, m_data.m_g, m_data.m_b);
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetSkylightBottomColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSkylightBottomColor " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->SphereLight().m_ColorBottom = glm::vec3(m_data.m_r, m_data.m_g, m_data.m_b);
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetLightPosCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetLightPos " << m_data.m_r << " " << m_data.m_theta << " " << m_data.m_phi;
  c->m_appScene->AreaLight().m_Distance = m_data.m_r;
  c->m_appScene->AreaLight().m_Theta = m_data.m_theta;
  c->m_appScene->AreaLight().m_Phi = m_data.m_phi;
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetLightColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetLightColor " << m_data.m_index << " " << m_data.m_r << " " << m_data.m_g << " " << m_data.m_b;
  c->m_appScene->AreaLight().m_Color = glm::vec3(m_data.m_r, m_data.m_g, m_data.m_b);
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetLightSizeCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetLightSize " << m_data.m_index << " " << m_data.m_x << " " << m_data.m_y;
  c->m_appScene->AreaLight().m_Width = m_data.m_x;
  c->m_appScene->AreaLight().m_Height = m_data.m_y;
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
SetClipRegionCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetClipRegion " << m_data.m_minx << " " << m_data.m_maxx << " " << m_data.m_miny << " " << m_data.m_maxy
            << " " << m_data.m_minz << " " << m_data.m_maxz;
  c->m_appScene->m_roi.SetMinP(glm::vec3(m_data.m_minx, m_data.m_miny, m_data.m_minz));
  c->m_appScene->m_roi.SetMaxP(glm::vec3(m_data.m_maxx, m_data.m_maxy, m_data.m_maxz));
  c->m_renderSettings->m_DirtyFlags.SetFlag(RoiDirty);
}
void
SetVoxelScaleCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetVoxelScale " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z;
  c->m_appScene->m_volume->setPhysicalSize(m_data.m_x, m_data.m_y, m_data.m_z);
  // update things that depend on this scaling!
  c->m_appScene->initBoundsFromImg(c->m_appScene->m_volume);
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}
void
AutoThresholdCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "AutoThreshold " << m_data.m_channel << " " << m_data.m_method;
  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];

  switch (m_data.m_method) {
    case 0:
      // TODO generate custom control points for a CUSTOM GradientEditMode
      c->m_appScene->m_volume->channel(m_data.m_channel)->generate_auto2();
      break;
    case 1:
      // TODO generate custom control points for a CUSTOM GradientEditMode
      c->m_appScene->m_volume->channel(m_data.m_channel)->generate_auto();
      break;
    case 2:
      // TODO generate custom control points for a CUSTOM GradientEditMode
      c->m_appScene->m_volume->channel(m_data.m_channel)->generate_bestFit();
      break;
    case 3:
      // TODO generate custom control points for a CUSTOM GradientEditMode
      c->m_appScene->m_volume->channel(m_data.m_channel)->generate_chimerax();
      break;
    case 4:
      lutInfo.m_activeMode = GradientEditMode::PERCENTILE;
      lutInfo.m_pctLow = Histogram::DEFAULT_PCT_LOW;
      lutInfo.m_pctHigh = Histogram::DEFAULT_PCT_HIGH;
      c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
      break;
    default:
      LOG_WARNING << "AutoThreshold got unexpected method parameter " << m_data.m_method;
      lutInfo.m_activeMode = GradientEditMode::PERCENTILE;
      lutInfo.m_pctLow = Histogram::DEFAULT_PCT_LOW;
      lutInfo.m_pctHigh = Histogram::DEFAULT_PCT_HIGH;
      c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
      break;
  }
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}
void
SetPercentileThresholdCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetPercentileThreshold " << m_data.m_channel << " " << m_data.m_pctLow << " " << m_data.m_pctHigh;
  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];
  lutInfo.m_activeMode = GradientEditMode::PERCENTILE;
  lutInfo.m_pctLow = m_data.m_pctLow;
  lutInfo.m_pctHigh = m_data.m_pctHigh;
  c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}
void
SetOpacityCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetOpacity " << m_data.m_channel << " " << m_data.m_opacity;
  c->m_appScene->m_material.m_opacity[m_data.m_channel] = m_data.m_opacity;
  c->m_renderSettings->SetNoIterations(0);
}
void
SetPrimaryRayStepSizeCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetPrimaryRayStepSize " << m_data.m_stepSize;
  c->m_renderSettings->m_RenderSettings.m_StepSizeFactor = m_data.m_stepSize;
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}
void
SetSecondaryRayStepSizeCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSecondaryRayStepSize " << m_data.m_stepSize;
  c->m_renderSettings->m_RenderSettings.m_StepSizeFactorShadow = m_data.m_stepSize;
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}
void
SetBackgroundColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetBackgroundColor " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  c->m_appScene->m_material.m_backgroundColor[0] = m_data.m_r;
  c->m_appScene->m_material.m_backgroundColor[1] = m_data.m_g;
  c->m_appScene->m_material.m_backgroundColor[2] = m_data.m_b;
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
SetIsovalueThresholdCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetIsovalueThreshold " << m_data.m_channel << " " << m_data.m_isovalue << " " << m_data.m_isorange;

  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];
  lutInfo.m_activeMode = GradientEditMode::ISOVALUE;
  lutInfo.m_isorange = m_data.m_isorange;
  lutInfo.m_isovalue = m_data.m_isovalue;
  c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
SetControlPointsCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetControlPoints " << m_data.m_channel;
  // TODO debug print the data
  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];
  lutInfo.m_activeMode = GradientEditMode::CUSTOM;
  std::vector<LutControlPoint> stops;
  // 5 floats per stop.  first is position, next four are rgba.  use a only, for now.
  // TODO SHOULD PARSE DO THIS JOB?
  for (size_t i = 0; i < m_data.m_data.size() / 5; ++i) {
    stops.push_back({ m_data.m_data[i * 5], m_data.m_data[i * 5 + 4] });
  }

  lutInfo.m_customControlPoints = stops;
  std::sort(lutInfo.m_customControlPoints.begin(),
            lutInfo.m_customControlPoints.end(),
            [](const LutControlPoint& a, const LutControlPoint& b) { return a.first < b.first; });
  c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);

  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
LoadVolumeFromFileCommand::execute(ExecutionContext* c)
{
  LOG_WARNING << "LoadVolumeFromFile command is deprecated. Prefer LoadData command.";
  LOG_DEBUG << "LoadVolumeFromFile command: " << m_data.m_path << " S=" << m_data.m_scene << " T=" << m_data.m_time;
  struct STAT64_STRUCT buf;
  if (STAT64_FUNCTION(m_data.m_path.c_str(), &buf) == 0) {
    // TODO load metadata dims first
    // TODO can we load time sequences of separate files here?
    std::unique_ptr<IFileReader> reader(FileReader::getReader(m_data.m_path));
    if (!reader) {
      LOG_ERROR << "Could not find a reader for file " << m_data.m_path;
      return;
    }

    VolumeDimensions dims = reader->loadDimensions(m_data.m_path, m_data.m_scene);

    LoadSpec loadSpec;
    loadSpec.filepath = m_data.m_path;
    loadSpec.time = m_data.m_time;
    loadSpec.scene = m_data.m_scene;
    std::shared_ptr<ImageXYZC> image = reader->loadFromFile(loadSpec);
    if (!image) {
      return;
    }

    c->m_loadSpec = loadSpec;

    c->m_appScene->m_timeLine.setRange(0, dims.sizeT - 1);
    c->m_appScene->m_timeLine.setCurrentTime(m_data.m_time);
    c->m_appScene->m_timeLine.setTimeUnit(dims.timeUnit);
    c->m_appScene->m_timeLine.setTimeUnits(dims.timeUnits);

    c->m_appScene->m_volume = image;
    c->m_appScene->initSceneFromImg(image);

    // Tell the camera about the volume's bounding box
    c->m_camera->m_SceneBoundingBox.m_MinP = c->m_appScene->m_boundingBox.GetMinP();
    c->m_camera->m_SceneBoundingBox.m_MaxP = c->m_appScene->m_boundingBox.GetMaxP();
    c->m_camera->SetViewMode(ViewModeFront);

    // enable initial channels
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      c->m_appScene->m_material.m_enabled[i] = (i < ImageXYZC::FIRST_N_CHANNELS);
      c->m_appScene->m_material.m_opacity[i] = 1.0f;
    }
    c->m_renderSettings->SetNoIterations(0);
    c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
    c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);

    // fire back some json immediately...
    nlohmann::json j;
    j["commandId"] = (int)LoadVolumeFromFileCommand::m_ID;
    j["x"] = (int)image->sizeX();
    j["y"] = (int)image->sizeY();
    j["z"] = (int)image->sizeZ();
    j["c"] = (int)image->sizeC();
    j["t"] = 1;
    j["pixel_size_x"] = image->physicalSizeX();
    j["pixel_size_y"] = image->physicalSizeY();
    j["pixel_size_z"] = image->physicalSizeZ();
    std::vector<std::string> channelNames;
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      channelNames.push_back((image->channel(i)->m_name));
    }
    j["channel_names"] = channelNames;
    std::vector<uint16_t> channelMaxIntensity;
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
      channelMaxIntensity.push_back(image->channel(i)->m_histogram.getDataMax());
    }
    j["channel_max_intensity"] = channelMaxIntensity;

    c->m_message = j.dump();
  } else {
    LOG_WARNING << "stat failed on image with errno " << errno;
    LOG_WARNING << "Image will not be loaded";
  }
}

void
SetTimeCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetTime command: "
            << " T=" << m_data.m_time;

  // setting same time is a no-op.
  if (m_data.m_time == c->m_appScene->m_timeLine.currentTime()) {
    return;
  }

  LoadSpec loadSpec = c->m_loadSpec;
  loadSpec.time = m_data.m_time;
  std::shared_ptr<ImageXYZC> image;
  try {

    image = FileReader::loadAndCache(loadSpec);
  } catch (...) {
    LOG_ERROR << "Failed to load time " << m_data.m_time << " from file " << c->m_loadSpec.toString();
    image = nullptr;
  }
  if (!image) {
    LOG_WARNING << "SetTime command called without a file loaded";
    return;
  }

  // successfully loaded; update loadspec in context
  c->m_loadSpec = loadSpec;

  c->m_appScene->m_timeLine.setCurrentTime(m_data.m_time);

  // we expect the scene volume dimensions to be the same; we want to preserve all view settings here.
  // BUT we want to convert the old lookup tables to new lookup tables
  // if we are preserving absolute transfer function settings

  // require sizeC to be the same for both previous image and new image
  if (image->sizeC() != c->m_appScene->m_volume->sizeC()) {
    LOG_ERROR << "Channel count mismatch for different times in same file";
  }

  // remap LUTs to preserve absolute thresholding
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[i];
    lutInfo.convert(c->m_appScene->m_volume->channel(i)->m_histogram, image->channel(i)->m_histogram);
    image->channel(i)->generateFromGradientData(lutInfo);
  }

  // now we're ready to lose the old channel histograms
  c->m_appScene->m_volume = image;

  c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);

  // fire back some json immediately...
  nlohmann::json j;
  j["commandId"] = (int)SetTimeCommand::m_ID;
  std::vector<uint16_t> channelMaxIntensity;
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    channelMaxIntensity.push_back(image->channel(i)->m_histogram.getDataMax());
  }
  j["channel_max_intensity"] = channelMaxIntensity;

  c->m_message = j.dump();
}

void
SetBoundingBoxColorCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetBoundingBoxColor " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  c->m_appScene->m_material.m_boundingBoxColor[0] = m_data.m_r;
  c->m_appScene->m_material.m_boundingBoxColor[1] = m_data.m_g;
  c->m_appScene->m_material.m_boundingBoxColor[2] = m_data.m_b;
}

void
ShowBoundingBoxCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "ShowBoundingBox " << m_data.m_on;
  c->m_appScene->m_material.m_showBoundingBox = m_data.m_on ? true : false;
}

void
TrackballCameraCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "TrackballCamera " << m_data.m_theta << " " << m_data.m_phi;
  c->m_camera->Trackball(m_data.m_theta, m_data.m_phi);
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
}

void
LoadDataCommand::execute(ExecutionContext* c)
{
  // TODO handle errors in a client/server remote situation

  LOG_DEBUG << "LoadData " << m_data.m_path << " " << m_data.m_scene << " " << m_data.m_level << " " << m_data.m_time;
  c->m_loadSpec.filepath = m_data.m_path;
  c->m_loadSpec.scene = m_data.m_scene;
  c->m_loadSpec.subpath = std::to_string(m_data.m_level);
  c->m_loadSpec.time = m_data.m_time;
  c->m_loadSpec.channels = std::vector<uint32_t>(m_data.m_channels.begin(), m_data.m_channels.end());
  c->m_loadSpec.minx = m_data.m_xmin;
  c->m_loadSpec.maxx = m_data.m_xmax;
  c->m_loadSpec.miny = m_data.m_ymin;
  c->m_loadSpec.maxy = m_data.m_ymax;
  c->m_loadSpec.minz = m_data.m_zmin;
  c->m_loadSpec.maxz = m_data.m_zmax;

  // TODO can we load time sequences of separate files here?
  std::shared_ptr<IFileReader> reader(FileReader::getReader(m_data.m_path));
  if (!reader) {
    LOG_ERROR << "Could not find a reader for file " << m_data.m_path;
    return;
  }

  VolumeDimensions dims = reader->loadDimensions(m_data.m_path, m_data.m_scene);

  std::shared_ptr<ImageXYZC> image = FileReader::loadAndCache(c->m_loadSpec, reader);
  if (!image) {
    return;
  }

  c->m_appScene->m_timeLine.setRange(0, dims.sizeT - 1);
  c->m_appScene->m_timeLine.setCurrentTime(m_data.m_time);
  c->m_appScene->m_timeLine.setTimeUnit(dims.timeUnit);
  c->m_appScene->m_timeLine.setTimeUnits(dims.timeUnits);

  c->m_appScene->m_volume = image;
  c->m_appScene->initSceneFromImg(image);

  // Tell the camera about the volume's bounding box
  c->m_camera->m_SceneBoundingBox.m_MinP = c->m_appScene->m_boundingBox.GetMinP();
  c->m_camera->m_SceneBoundingBox.m_MaxP = c->m_appScene->m_boundingBox.GetMaxP();
  c->m_camera->SetViewMode(ViewModeFront);

  // TODO should we be modifying any of this state???
  // why not retain previous channel enabled state

  // enable initial channels
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    c->m_appScene->m_material.m_enabled[i] = (i < ImageXYZC::FIRST_N_CHANNELS);
    c->m_appScene->m_material.m_opacity[i] = 1.0f;
  }
  c->m_renderSettings->SetNoIterations(0);
  c->m_renderSettings->m_DirtyFlags.SetFlag(CameraDirty);
  c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDirty);
  c->m_renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);

  // fire back some json immediately...
  nlohmann::json j;
  j["commandId"] = (int)LoadDataCommand::m_ID;
  j["x"] = (int)image->sizeX();
  j["y"] = (int)image->sizeY();
  j["z"] = (int)image->sizeZ();
  j["c"] = (int)image->sizeC();
  j["t"] = (int)dims.sizeT;
  j["pixel_size_x"] = image->physicalSizeX();
  j["pixel_size_y"] = image->physicalSizeY();
  j["pixel_size_z"] = image->physicalSizeZ();
  // Full VolumeDimensions metadata for the loaded image.
  nlohmann::json vd;
  vd["size_x"] = (int)dims.sizeX;
  vd["size_y"] = (int)dims.sizeY;
  vd["size_z"] = (int)dims.sizeZ;
  vd["size_c"] = (int)dims.sizeC;
  vd["size_t"] = (int)dims.sizeT;
  vd["physical_size_x"] = dims.physicalSizeX;
  vd["physical_size_y"] = dims.physicalSizeY;
  vd["physical_size_z"] = dims.physicalSizeZ;
  vd["spatial_units"] = dims.spatialUnits;
  vd["time_unit"] = dims.timeUnit;
  vd["time_units"] = dims.timeUnits;
  vd["bits_per_pixel"] = (int)dims.bitsPerPixel;
  vd["sample_format"] = (int)dims.sampleFormat;
  vd["dimension_order"] = dims.dimensionOrder;
  vd["channel_names"] = dims.channelNames;
  j["volume_dimensions"] = vd;
  std::vector<std::string> channelNames;
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    channelNames.push_back((image->channel(i)->m_name));
  }
  j["channel_names"] = channelNames;
  std::vector<uint16_t> channelMinIntensity;
  std::vector<uint16_t> channelMaxIntensity;
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    channelMinIntensity.push_back(image->channel(i)->m_histogram.getDataMin());
    channelMaxIntensity.push_back(image->channel(i)->m_histogram.getDataMax());
  }
  j["channel_min_intensity"] = channelMinIntensity;
  j["channel_max_intensity"] = channelMaxIntensity;

  c->m_message = j.dump();
}

void
ShowScaleBarCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "ShowScaleBar " << m_data.m_on;
  c->m_appScene->m_showScaleBar = m_data.m_on ? true : false;
}

void
SetFlipAxisCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetFlipAxis " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z;
  c->m_appScene->m_volume->setVolumeAxesFlipped(m_data.m_x, m_data.m_y, m_data.m_z);
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
SetInterpolationCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetInterpolation " << m_data.m_on;
  c->m_renderSettings->m_RenderSettings.m_InterpolatedVolumeSampling = m_data.m_on;
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
SetClipPlaneCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetClipPlane " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z << " " << m_data.m_w;
  // get the transform from the default clip plane.
  Plane p(glm::vec3(m_data.m_x, m_data.m_y, m_data.m_z), m_data.m_w);
  Plane p0;
  Transform3d tr = p0.getTransformTo(p);
  c->m_appScene->m_clipPlane->m_plane = p0;
  c->m_appScene->m_clipPlane->m_transform = tr;
  c->m_appScene->m_clipPlane->m_enabled = true;
  c->m_appScene->m_clipPlane->updateTransform();
  c->m_renderSettings->m_DirtyFlags.SetFlag(RenderParamsDirty);
}

void
SetColorRampCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetColorRamp " << m_data.m_channel;
  // TODO debug print the data

  // this channel's current ramp.
  ColorRamp& ramp = c->m_appScene->m_material.m_colormap[m_data.m_channel];
  ramp.m_name = m_data.m_name;

  // We should see if it's one of our named ramps.
  // colormapFromName always returns something, so we need to compare the name to "none" to see if it's a known ramp.
  auto foundRamp = ColorRamp::colormapFromName(m_data.m_name);
  if (foundRamp.m_name != ColorRamp::NO_COLORMAP_NAME ||
      (foundRamp.m_name == ColorRamp::NO_COLORMAP_NAME && m_data.m_name == ColorRamp::NO_COLORMAP_NAME)) {
    ramp = foundRamp;
  } else {
    // this is actually a custom ramp and we need to build it up from the data.
    std::vector<ColorControlPoint> stops;
    // 5 floats per stop.  first is position, next four are rgba.
    for (size_t i = 0; i < m_data.m_data.size() / 5; ++i) {
      stops.push_back({ m_data.m_data[i * 5],
                        m_data.m_data[i * 5 + 1],
                        m_data.m_data[i * 5 + 2],
                        m_data.m_data[i * 5 + 3],
                        m_data.m_data[i * 5 + 4] });
    }
    ramp.updateStops(stops);
  }

  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
SetMinMaxThresholdCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetMinMaxThreshold " << m_data.m_channel << " " << m_data.m_min << " " << m_data.m_max;
  GradientData& lutInfo = c->m_appScene->m_material.m_gradientData[m_data.m_channel];
  lutInfo.m_activeMode = GradientEditMode::MINMAX;
  lutInfo.m_minu16 = m_data.m_min;
  lutInfo.m_maxu16 = m_data.m_max;
  c->m_appScene->m_volume->channel(m_data.m_channel)->generateFromGradientData(lutInfo);
  c->m_renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
}

void
SetSkylightRotationCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetSkylightRotation " << m_data.m_x << " " << m_data.m_y << " " << m_data.m_z << " " << m_data.m_w;
  // glm::quat ctor order is (w, x, y, z)
  glm::quat q = glm::normalize(glm::quat(m_data.m_w, m_data.m_x, m_data.m_y, m_data.m_z));
  c->m_appScene->SceneSphereLight()->m_transform.m_rotation = q;
  c->m_appScene->SceneSphereLight()->updateTransform();
  c->m_renderSettings->m_DirtyFlags.SetFlag(LightsDirty);
}
void
ShowTimeStampCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "ShowTimeStamp " << m_data.m_on;
  c->m_appScene->m_showTimeStamp = m_data.m_on ? true : false;
}

void
SetTimeStampFormatCommand::execute(ExecutionContext* c)
{
  LOG_DEBUG << "SetTimeStampFormat " << m_data.m_format;
  constexpr int32_t kMinMode = static_cast<int32_t>(Scene::TimeStampDisplayMode::HHMMSS);
  constexpr int32_t kMaxMode = static_cast<int32_t>(Scene::TimeStampDisplayMode::TIME_UNITS);
  if (m_data.m_format < kMinMode || m_data.m_format > kMaxMode) {
    LOG_WARNING << "SetTimeStampFormat: invalid display mode " << m_data.m_format << ", ignoring";
    return;
  }
  c->m_appScene->m_timeStampDisplayMode = static_cast<Scene::TimeStampDisplayMode>(m_data.m_format);
}

SessionCommand*
SessionCommand::parse(ParseableStream* c)
{
  SessionCommandD data;
  data.m_name = c->parseString();
  return new SessionCommand(data);
}

size_t
SessionCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeString(m_data.m_name);
  return bytesWritten;
}

AssetPathCommand*
AssetPathCommand::parse(ParseableStream* c)
{
  AssetPathCommandD data;
  data.m_name = c->parseString();
  return new AssetPathCommand(data);
}
size_t
AssetPathCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeString(m_data.m_name);
  return bytesWritten;
}

LoadOmeTifCommand*
LoadOmeTifCommand::parse(ParseableStream* c)
{
  LOG_WARNING << "LoadOmeTif command is deprecated. Prefer LoadData command.";
  LoadOmeTifCommandD data;
  data.m_name = c->parseString();
  return new LoadOmeTifCommand(data);
}
size_t
LoadOmeTifCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeString(m_data.m_name);
  return bytesWritten;
}

SetCameraPosCommand*
SetCameraPosCommand::parse(ParseableStream* c)
{
  SetCameraPosCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  return new SetCameraPosCommand(data);
}
size_t
SetCameraPosCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  return bytesWritten;
}

SetCameraUpCommand*
SetCameraUpCommand::parse(ParseableStream* c)
{
  SetCameraUpCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  return new SetCameraUpCommand(data);
}
size_t
SetCameraUpCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  return bytesWritten;
}

SetCameraTargetCommand*
SetCameraTargetCommand::parse(ParseableStream* c)
{
  SetCameraTargetCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  return new SetCameraTargetCommand(data);
}
size_t
SetCameraTargetCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  return bytesWritten;
}

SetCameraApertureCommand*
SetCameraApertureCommand::parse(ParseableStream* c)
{
  SetCameraApertureCommandD data;
  data.m_x = c->parseFloat32();
  return new SetCameraApertureCommand(data);
}
size_t
SetCameraApertureCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  return bytesWritten;
}

SetCameraProjectionCommand*
SetCameraProjectionCommand::parse(ParseableStream* c)
{
  SetCameraProjectionCommandD data;
  data.m_projectionType = c->parseInt32();
  data.m_x = c->parseFloat32();
  return new SetCameraProjectionCommand(data);
}
size_t
SetCameraProjectionCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_projectionType);
  bytesWritten += o->writeFloat32(m_data.m_x);
  return bytesWritten;
}

SetCameraFocalDistanceCommand*
SetCameraFocalDistanceCommand::parse(ParseableStream* c)
{
  SetCameraFocalDistanceCommandD data;
  data.m_x = c->parseFloat32();
  return new SetCameraFocalDistanceCommand(data);
}
size_t
SetCameraFocalDistanceCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  return bytesWritten;
}

SetCameraExposureCommand*
SetCameraExposureCommand::parse(ParseableStream* c)
{
  SetCameraExposureCommandD data;
  data.m_x = c->parseFloat32();
  return new SetCameraExposureCommand(data);
}
size_t
SetCameraExposureCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  return bytesWritten;
}

SetDiffuseColorCommand*
SetDiffuseColorCommand::parse(ParseableStream* c)
{
  SetDiffuseColorCommandD data;
  data.m_channel = c->parseInt32();
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  data.m_a = c->parseFloat32();
  return new SetDiffuseColorCommand(data);
}
size_t
SetDiffuseColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  bytesWritten += o->writeFloat32(m_data.m_a);
  return bytesWritten;
}

SetSpecularColorCommand*
SetSpecularColorCommand::parse(ParseableStream* c)
{
  SetSpecularColorCommandD data;
  data.m_channel = c->parseInt32();
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  data.m_a = c->parseFloat32();
  return new SetSpecularColorCommand(data);
}
size_t
SetSpecularColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  bytesWritten += o->writeFloat32(m_data.m_a);
  return bytesWritten;
}

SetEmissiveColorCommand*
SetEmissiveColorCommand::parse(ParseableStream* c)
{
  SetEmissiveColorCommandD data;
  data.m_channel = c->parseInt32();
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  data.m_a = c->parseFloat32();
  return new SetEmissiveColorCommand(data);
}
size_t
SetEmissiveColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  bytesWritten += o->writeFloat32(m_data.m_a);
  return bytesWritten;
}

SetRenderIterationsCommand*
SetRenderIterationsCommand::parse(ParseableStream* c)
{
  SetRenderIterationsCommandD data;
  data.m_x = c->parseInt32();
  return new SetRenderIterationsCommand(data);
}
size_t
SetRenderIterationsCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_x);
  return bytesWritten;
}

SetStreamModeCommand*
SetStreamModeCommand::parse(ParseableStream* c)
{
  SetStreamModeCommandD data;
  data.m_x = c->parseInt32();
  return new SetStreamModeCommand(data);
}
size_t
SetStreamModeCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_x);
  return bytesWritten;
}

RequestRedrawCommand*
RequestRedrawCommand::parse(ParseableStream* c)
{
  RequestRedrawCommandD data;
  return new RequestRedrawCommand(data);
}
size_t
RequestRedrawCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  return bytesWritten;
}

SetResolutionCommand*
SetResolutionCommand::parse(ParseableStream* c)
{
  SetResolutionCommandD data;
  data.m_x = c->parseInt32();
  data.m_y = c->parseInt32();
  return new SetResolutionCommand(data);
}
size_t
SetResolutionCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_x);
  bytesWritten += o->writeInt32(m_data.m_y);
  return bytesWritten;
}

SetDensityCommand*
SetDensityCommand::parse(ParseableStream* c)
{
  SetDensityCommandD data;
  data.m_x = c->parseFloat32();
  return new SetDensityCommand(data);
}
size_t
SetDensityCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  return bytesWritten;
}

FrameSceneCommand*
FrameSceneCommand::parse(ParseableStream* c)
{
  FrameSceneCommandD data;
  return new FrameSceneCommand(data);
}
size_t
FrameSceneCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  return bytesWritten;
}

SetGlossinessCommand*
SetGlossinessCommand::parse(ParseableStream* c)
{
  SetGlossinessCommandD data;
  data.m_channel = c->parseInt32();
  data.m_glossiness = c->parseFloat32();
  return new SetGlossinessCommand(data);
}
size_t
SetGlossinessCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_glossiness);
  return bytesWritten;
}

EnableChannelCommand*
EnableChannelCommand::parse(ParseableStream* c)
{
  EnableChannelCommandD data;
  data.m_channel = c->parseInt32();
  data.m_enabled = c->parseInt32();
  return new EnableChannelCommand(data);
}
size_t
EnableChannelCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeInt32(m_data.m_enabled);
  return bytesWritten;
}

SetWindowLevelCommand*
SetWindowLevelCommand::parse(ParseableStream* c)
{
  SetWindowLevelCommandD data;
  data.m_channel = c->parseInt32();
  data.m_window = c->parseFloat32();
  data.m_level = c->parseFloat32();
  return new SetWindowLevelCommand(data);
}
size_t
SetWindowLevelCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_window);
  bytesWritten += o->writeFloat32(m_data.m_level);
  return bytesWritten;
}

OrbitCameraCommand*
OrbitCameraCommand::parse(ParseableStream* c)
{
  OrbitCameraCommandD data;
  data.m_theta = c->parseFloat32();
  data.m_phi = c->parseFloat32();
  return new OrbitCameraCommand(data);
}
size_t
OrbitCameraCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_theta);
  bytesWritten += o->writeFloat32(m_data.m_phi);
  return bytesWritten;
}

SetSkylightTopColorCommand*
SetSkylightTopColorCommand::parse(ParseableStream* c)
{
  SetSkylightTopColorCommandD data;
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetSkylightTopColorCommand(data);
}
size_t
SetSkylightTopColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

SetSkylightMiddleColorCommand*
SetSkylightMiddleColorCommand::parse(ParseableStream* c)
{
  SetSkylightMiddleColorCommandD data;
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetSkylightMiddleColorCommand(data);
}
size_t
SetSkylightMiddleColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

SetSkylightBottomColorCommand*
SetSkylightBottomColorCommand::parse(ParseableStream* c)
{
  SetSkylightBottomColorCommandD data;
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetSkylightBottomColorCommand(data);
}
size_t
SetSkylightBottomColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

SetLightPosCommand*
SetLightPosCommand::parse(ParseableStream* c)
{
  SetLightPosCommandD data;
  data.m_index = c->parseInt32();
  data.m_r = c->parseFloat32();
  data.m_theta = c->parseFloat32();
  data.m_phi = c->parseFloat32();
  return new SetLightPosCommand(data);
}
size_t
SetLightPosCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_index);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_theta);
  bytesWritten += o->writeFloat32(m_data.m_phi);
  return bytesWritten;
}

SetLightColorCommand*
SetLightColorCommand::parse(ParseableStream* c)
{
  SetLightColorCommandD data;
  data.m_index = c->parseInt32();
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetLightColorCommand(data);
}
size_t
SetLightColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_index);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

SetLightSizeCommand*
SetLightSizeCommand::parse(ParseableStream* c)
{
  SetLightSizeCommandD data;
  data.m_index = c->parseInt32();
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  return new SetLightSizeCommand(data);
}
size_t
SetLightSizeCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_index);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  return bytesWritten;
}

float
clamp(float x, float bottom, float top)
{
  return std::min(top, std::max(bottom, x));
}

SetClipRegionCommand*
SetClipRegionCommand::parse(ParseableStream* c)
{
  SetClipRegionCommandD data;
  data.m_minx = c->parseFloat32();
  data.m_minx = clamp(data.m_minx, 0.0, 1.0);
  data.m_maxx = c->parseFloat32();
  data.m_maxx = clamp(data.m_maxx, 0.0, 1.0);
  data.m_miny = c->parseFloat32();
  data.m_miny = clamp(data.m_miny, 0.0, 1.0);
  data.m_maxy = c->parseFloat32();
  data.m_maxy = clamp(data.m_maxy, 0.0, 1.0);
  data.m_minz = c->parseFloat32();
  data.m_minz = clamp(data.m_minz, 0.0, 1.0);
  data.m_maxz = c->parseFloat32();
  data.m_maxz = clamp(data.m_maxz, 0.0, 1.0);
  return new SetClipRegionCommand(data);
}
size_t
SetClipRegionCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_minx);
  bytesWritten += o->writeFloat32(m_data.m_maxx);
  bytesWritten += o->writeFloat32(m_data.m_miny);
  bytesWritten += o->writeFloat32(m_data.m_maxy);
  bytesWritten += o->writeFloat32(m_data.m_minz);
  bytesWritten += o->writeFloat32(m_data.m_maxz);
  return bytesWritten;
}

SetVoxelScaleCommand*
SetVoxelScaleCommand::parse(ParseableStream* c)
{
  SetVoxelScaleCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  return new SetVoxelScaleCommand(data);
}
size_t
SetVoxelScaleCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  return bytesWritten;
}

AutoThresholdCommand*
AutoThresholdCommand::parse(ParseableStream* c)
{
  AutoThresholdCommandD data;
  data.m_channel = c->parseInt32();
  data.m_method = c->parseInt32();
  return new AutoThresholdCommand(data);
}
size_t
AutoThresholdCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeInt32(m_data.m_method);
  return bytesWritten;
}

SetPercentileThresholdCommand*
SetPercentileThresholdCommand::parse(ParseableStream* c)
{
  SetPercentileThresholdCommandD data;
  data.m_channel = c->parseInt32();
  data.m_pctLow = c->parseFloat32();
  data.m_pctHigh = c->parseFloat32();
  return new SetPercentileThresholdCommand(data);
}
size_t
SetPercentileThresholdCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_pctLow);
  bytesWritten += o->writeFloat32(m_data.m_pctHigh);
  return bytesWritten;
}

SetOpacityCommand*
SetOpacityCommand::parse(ParseableStream* c)
{
  SetOpacityCommandD data;
  data.m_channel = c->parseInt32();
  data.m_opacity = c->parseFloat32();
  return new SetOpacityCommand(data);
}
size_t
SetOpacityCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_opacity);
  return bytesWritten;
}

SetPrimaryRayStepSizeCommand*
SetPrimaryRayStepSizeCommand::parse(ParseableStream* c)
{
  SetPrimaryRayStepSizeCommandD data;
  data.m_stepSize = c->parseFloat32();
  return new SetPrimaryRayStepSizeCommand(data);
}
size_t
SetPrimaryRayStepSizeCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_stepSize);
  return bytesWritten;
}

SetSecondaryRayStepSizeCommand*
SetSecondaryRayStepSizeCommand::parse(ParseableStream* c)
{
  SetSecondaryRayStepSizeCommandD data;
  data.m_stepSize = c->parseFloat32();
  return new SetSecondaryRayStepSizeCommand(data);
}
size_t
SetSecondaryRayStepSizeCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_stepSize);
  return bytesWritten;
}

SetBackgroundColorCommand*
SetBackgroundColorCommand::parse(ParseableStream* c)
{
  SetBackgroundColorCommandD data;
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetBackgroundColorCommand(data);
}
size_t
SetBackgroundColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

SetIsovalueThresholdCommand*
SetIsovalueThresholdCommand::parse(ParseableStream* c)
{
  SetIsovalueThresholdCommandD data;
  data.m_channel = c->parseInt32();
  data.m_isovalue = c->parseFloat32();
  data.m_isorange = c->parseFloat32();
  return new SetIsovalueThresholdCommand(data);
}
size_t
SetIsovalueThresholdCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32(m_data.m_isovalue);
  bytesWritten += o->writeFloat32(m_data.m_isorange);
  return bytesWritten;
}

SetControlPointsCommand*
SetControlPointsCommand::parse(ParseableStream* c)
{
  SetControlPointsCommandD data;
  data.m_channel = c->parseInt32();
  data.m_data = c->parseFloat32Array();
  return new SetControlPointsCommand(data);
}
size_t
SetControlPointsCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeFloat32Array(m_data.m_data);
  return bytesWritten;
}

LoadVolumeFromFileCommand*
LoadVolumeFromFileCommand::parse(ParseableStream* c)
{
  LOG_WARNING << "LoadVolumeFromFile command is deprecated. Prefer LoadData command.";
  LoadVolumeFromFileCommandD data;
  data.m_path = c->parseString();
  data.m_scene = c->parseInt32();
  data.m_time = c->parseInt32();
  return new LoadVolumeFromFileCommand(data);
}
size_t
LoadVolumeFromFileCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeString(m_data.m_path);
  bytesWritten += o->writeInt32(m_data.m_scene);
  bytesWritten += o->writeInt32(m_data.m_time);
  return bytesWritten;
}

SetTimeCommand*
SetTimeCommand::parse(ParseableStream* c)
{
  SetTimeCommandD data;
  data.m_time = c->parseInt32();
  return new SetTimeCommand(data);
}
size_t
SetTimeCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_time);
  return bytesWritten;
}

SetBoundingBoxColorCommand*
SetBoundingBoxColorCommand::parse(ParseableStream* c)
{
  SetBoundingBoxColorCommandD data;
  data.m_r = c->parseFloat32();
  data.m_g = c->parseFloat32();
  data.m_b = c->parseFloat32();
  return new SetBoundingBoxColorCommand(data);
}
size_t
SetBoundingBoxColorCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_r);
  bytesWritten += o->writeFloat32(m_data.m_g);
  bytesWritten += o->writeFloat32(m_data.m_b);
  return bytesWritten;
}

ShowBoundingBoxCommand*
ShowBoundingBoxCommand::parse(ParseableStream* c)
{
  ShowBoundingBoxCommandD data;
  data.m_on = c->parseInt32();
  return new ShowBoundingBoxCommand(data);
}
size_t
ShowBoundingBoxCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_on);
  return bytesWritten;
}

TrackballCameraCommand*
TrackballCameraCommand::parse(ParseableStream* c)
{
  TrackballCameraCommandD data;
  data.m_theta = c->parseFloat32();
  data.m_phi = c->parseFloat32();
  return new TrackballCameraCommand(data);
}
size_t
TrackballCameraCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_theta);
  bytesWritten += o->writeFloat32(m_data.m_phi);
  return bytesWritten;
}

LoadDataCommand*
LoadDataCommand::parse(ParseableStream* c)
{
  LoadDataCommandD data;
  data.m_path = c->parseString();
  data.m_scene = c->parseInt32();
  data.m_level = c->parseInt32();
  data.m_time = c->parseInt32();
  data.m_channels = c->parseInt32Array();
  std::vector<int32_t> region = c->parseInt32Array();
  // load from array only if complete.
  if (region.size() == 6) {
    data.m_xmin = region[0];
    data.m_xmax = region[1];
    data.m_ymin = region[2];
    data.m_ymax = region[3];
    data.m_zmin = region[4];
    data.m_zmax = region[5];
  } else {
    data.m_xmax = 0;
    data.m_xmin = 0;
    data.m_ymax = 0;
    data.m_ymin = 0;
    data.m_zmax = 0;
    data.m_zmin = 0;

    if (!region.empty()) {
      LOG_ERROR << "Bad region data for LoadDataCommand";
    }
  }
  return new LoadDataCommand(data);
}
size_t
LoadDataCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeString(m_data.m_path);
  bytesWritten += o->writeInt32(m_data.m_scene);
  bytesWritten += o->writeInt32(m_data.m_level);
  bytesWritten += o->writeInt32(m_data.m_time);
  bytesWritten += o->writeInt32Array(m_data.m_channels);
  bytesWritten +=
    o->writeInt32Array({ m_data.m_xmin, m_data.m_xmax, m_data.m_ymin, m_data.m_ymax, m_data.m_zmin, m_data.m_zmax });
  return bytesWritten;
}

ShowScaleBarCommand*
ShowScaleBarCommand::parse(ParseableStream* c)
{
  ShowScaleBarCommandD data;
  data.m_on = c->parseInt32();
  return new ShowScaleBarCommand(data);
}
size_t
ShowScaleBarCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_on);
  return bytesWritten;
}

SetFlipAxisCommand*
SetFlipAxisCommand::parse(ParseableStream* c)
{
  SetFlipAxisCommandD data;
  data.m_x = c->parseInt32();
  data.m_y = c->parseInt32();
  data.m_z = c->parseInt32();
  return new SetFlipAxisCommand(data);
}

size_t
SetFlipAxisCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_x);
  bytesWritten += o->writeInt32(m_data.m_y);
  bytesWritten += o->writeInt32(m_data.m_z);
  return bytesWritten;
}

SetInterpolationCommand*
SetInterpolationCommand::parse(ParseableStream* c)
{
  SetInterpolationCommandD data;
  data.m_on = c->parseInt32();
  return new SetInterpolationCommand(data);
}

size_t
SetInterpolationCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_on);
  return bytesWritten;
}

SetClipPlaneCommand*
SetClipPlaneCommand::parse(ParseableStream* c)
{
  SetClipPlaneCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  data.m_w = c->parseFloat32();
  return new SetClipPlaneCommand(data);
}

size_t
SetClipPlaneCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  bytesWritten += o->writeFloat32(m_data.m_w);
  return bytesWritten;
}

SetColorRampCommand*
SetColorRampCommand::parse(ParseableStream* c)
{
  SetColorRampCommandD data;
  data.m_channel = c->parseInt32();
  data.m_name = c->parseString();
  data.m_data = c->parseFloat32Array();
  return new SetColorRampCommand(data);
}
size_t
SetColorRampCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeString(m_data.m_name);
  bytesWritten += o->writeFloat32Array(m_data.m_data);
  return bytesWritten;
}

SetMinMaxThresholdCommand*
SetMinMaxThresholdCommand::parse(ParseableStream* c)
{
  SetMinMaxThresholdCommandD data;
  data.m_channel = c->parseInt32();
  data.m_min = c->parseInt32();
  data.m_max = c->parseInt32();
  return new SetMinMaxThresholdCommand(data);
}

size_t
SetMinMaxThresholdCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_channel);
  bytesWritten += o->writeInt32(m_data.m_min);
  bytesWritten += o->writeInt32(m_data.m_max);
  return bytesWritten;
}

SetSkylightRotationCommand*
SetSkylightRotationCommand::parse(ParseableStream* c)
{
  SetSkylightRotationCommandD data;
  data.m_x = c->parseFloat32();
  data.m_y = c->parseFloat32();
  data.m_z = c->parseFloat32();
  data.m_w = c->parseFloat32();
  return new SetSkylightRotationCommand(data);
}

size_t
SetSkylightRotationCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeFloat32(m_data.m_x);
  bytesWritten += o->writeFloat32(m_data.m_y);
  bytesWritten += o->writeFloat32(m_data.m_z);
  bytesWritten += o->writeFloat32(m_data.m_w);
  return bytesWritten;
}
ShowTimeStampCommand*
ShowTimeStampCommand::parse(ParseableStream* c)
{
  ShowTimeStampCommandD data;
  data.m_on = c->parseInt32();
  return new ShowTimeStampCommand(data);
}
size_t
ShowTimeStampCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_on);
  return bytesWritten;
}

SetTimeStampFormatCommand*
SetTimeStampFormatCommand::parse(ParseableStream* c)
{
  SetTimeStampFormatCommandD data;
  data.m_format = c->parseInt32();
  return new SetTimeStampFormatCommand(data);
}
size_t
SetTimeStampFormatCommand::write(WriteableStream* o) const
{
  size_t bytesWritten = 0;
  bytesWritten += o->writeInt32(m_ID);
  bytesWritten += o->writeInt32(m_data.m_format);
  return bytesWritten;
}

std::string
SessionCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << "\"" << escapePath(m_data.m_name) << "\"";
  ss << ")";
  return ss.str();
}
std::string
AssetPathCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << "\"" << m_data.m_name << "\"";
  ss << ")";
  return ss.str();
}
std::string
LoadOmeTifCommand::toPythonString() const
{
  LOG_WARNING << "LoadOmeTif command is deprecated. Prefer LoadData command.";
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << "\"" << m_data.m_name << "\"";
  ss << ")";
  return ss.str();
}
std::string
SetCameraPosCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z;
  ss << ")";
  return ss.str();
}
std::string
SetCameraUpCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z;
  ss << ")";
  return ss.str();
}
std::string
SetCameraTargetCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z;
  ss << ")";
  return ss.str();
}
std::string
SetCameraApertureCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
SetCameraProjectionCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_projectionType << ", " << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
SetCameraFocalDistanceCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
SetCameraExposureCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
SetDiffuseColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b << ", " << m_data.m_a;
  ss << ")";
  return ss.str();
}
std::string
SetSpecularColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b << ", " << m_data.m_a;
  ss << ")";
  return ss.str();
}
std::string
SetEmissiveColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b << ", " << m_data.m_a;
  ss << ")";
  return ss.str();
}
std::string
SetRenderIterationsCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
SetStreamModeCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
RequestRedrawCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << ")";
  return ss.str();
}
std::string
SetResolutionCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y;
  ss << ")";
  return ss.str();
}
std::string
SetDensityCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x;
  ss << ")";
  return ss.str();
}
std::string
FrameSceneCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << ")";
  return ss.str();
}
std::string
SetGlossinessCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_glossiness;
  ss << ")";
  return ss.str();
}
std::string
EnableChannelCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_enabled;
  ss << ")";
  return ss.str();
}
std::string
SetWindowLevelCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_window << ", " << m_data.m_level;
  ss << ")";
  return ss.str();
}
std::string
OrbitCameraCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_theta << ", " << m_data.m_phi;
  ss << ")";
  return ss.str();
}
std::string
SetSkylightTopColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}
std::string
SetSkylightMiddleColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}
std::string
SetSkylightBottomColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}

std::string
SetLightPosCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_index << ", " << m_data.m_r << ", " << m_data.m_theta << ", " << m_data.m_phi;
  ss << ")";
  return ss.str();
}
std::string
SetLightColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_index << ", " << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}
std::string
SetLightSizeCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_index << ", " << m_data.m_x << ", " << m_data.m_y;
  ss << ")";
  return ss.str();
}

std::string
SetClipRegionCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_minx << ", " << m_data.m_maxx << ", " << m_data.m_miny << ", " << m_data.m_maxy << ", "
     << m_data.m_minz << ", " << m_data.m_maxz;
  ss << ")";
  return ss.str();
}

std::string
SetVoxelScaleCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z;
  ss << ")";
  return ss.str();
}
std::string
AutoThresholdCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_method;
  ss << ")";
  return ss.str();
}
std::string
SetPercentileThresholdCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_pctLow << ", " << m_data.m_pctHigh;
  ss << ")";
  return ss.str();
}
std::string
SetOpacityCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_opacity;
  ss << ")";
  return ss.str();
}
std::string
SetPrimaryRayStepSizeCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_stepSize;
  ss << ")";
  return ss.str();
}
std::string
SetSecondaryRayStepSizeCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_stepSize;
  ss << ")";
  return ss.str();
}
std::string
SetBackgroundColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}
std::string
SetIsovalueThresholdCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_isovalue << ", " << m_data.m_isorange;
  ss << ")";
  return ss.str();
}
std::string
SetControlPointsCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << m_data.m_channel << ", [";
  // insert comma delimited but no comma after the last entry
  if (!m_data.m_data.empty()) {
    std::copy(m_data.m_data.begin(), std::prev(m_data.m_data.end()), std::ostream_iterator<float>(ss, ", "));
    ss << m_data.m_data.back();
  }
  ss << "]";

  ss << ")";
  return ss.str();
}

std::string
LoadVolumeFromFileCommand::toPythonString() const
{
  LOG_WARNING << "LoadVolumeFromFile command is deprecated. Prefer LoadData command.";
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << "\"" << m_data.m_path << "\", ";
  ss << m_data.m_scene << ", " << m_data.m_time;

  ss << ")";
  return ss.str();
}

std::string
SetTimeCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << m_data.m_time;

  ss << ")";
  return ss.str();
}

std::string
SetBoundingBoxColorCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_r << ", " << m_data.m_g << ", " << m_data.m_b;
  ss << ")";
  return ss.str();
}

std::string
ShowBoundingBoxCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << m_data.m_on;

  ss << ")";
  return ss.str();
}
std::string
TrackballCameraCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_theta << ", " << m_data.m_phi;
  ss << ")";
  return ss.str();
}
std::string
LoadDataCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  // escape slashes in path
  std::string escaped = escapePath(m_data.m_path);

  ss << "\"" << escaped << "\", ";
  ss << m_data.m_scene << ", " << m_data.m_level << ", " << m_data.m_time;
  ss << ", [";
  // insert comma delimited but no comma after the last entry
  if (!m_data.m_channels.empty()) {
    std::copy(m_data.m_channels.begin(), std::prev(m_data.m_channels.end()), std::ostream_iterator<int32_t>(ss, ", "));
    ss << m_data.m_channels.back();
  }
  ss << "], [";
  ss << m_data.m_xmin << ", " << m_data.m_xmax << ", " << m_data.m_ymin << ", " << m_data.m_ymax << ", "
     << m_data.m_zmin << ", " << m_data.m_zmax;
  ss << "]";

  ss << ")";
  return ss.str();
}
std::string
ShowScaleBarCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << m_data.m_on;

  ss << ")";
  return ss.str();
}

std::string
SetFlipAxisCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", ";
  ss << m_data.m_y << ", ";
  ss << m_data.m_z;
  ss << ")";
  return ss.str();
}

std::string
SetInterpolationCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_on;
  ss << ")";
  return ss.str();
}

std::string
SetClipPlaneCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z << ", " << m_data.m_w;
  ss << ")";
  return ss.str();
}

std::string
SetColorRampCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";

  ss << m_data.m_channel << ", \"" << m_data.m_name << "\", [";
  // insert comma delimited but no comma after the last entry
  if (!m_data.m_data.empty()) {
    std::copy(m_data.m_data.begin(), std::prev(m_data.m_data.end()), std::ostream_iterator<float>(ss, ", "));
    ss << m_data.m_data.back();
  }
  ss << "]";

  ss << ")";
  return ss.str();
}

std::string
SetMinMaxThresholdCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_channel << ", " << m_data.m_min << ", " << m_data.m_max;
  ss << ")";
  return ss.str();
}

std::string
SetSkylightRotationCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_x << ", " << m_data.m_y << ", " << m_data.m_z << ", " << m_data.m_w;
  ss << ")";
  return ss.str();
}
std::string
ShowTimeStampCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_on;
  ss << ")";
  return ss.str();
}

std::string
SetTimeStampFormatCommand::toPythonString() const
{
  std::ostringstream ss;
  ss << PythonName() << "(";
  ss << m_data.m_format;
  ss << ")";
  return ss.str();
}
