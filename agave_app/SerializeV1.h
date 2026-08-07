#pragma once

#include "renderlib/json/json.hpp"

#include <array>
#include <string>
#include <vector>

namespace Serialize {
struct PathTraceSettings_V1
{
  float primaryStepSize = 4.0f;
  float secondaryStepSize = 4.0f;
  int32_t multichannelBlendMode = 0; // 0 = Max, 1 = Weighted
  bool operator==(const PathTraceSettings_V1& other) const
  {
    return primaryStepSize == other.primaryStepSize && secondaryStepSize == other.secondaryStepSize &&
           multichannelBlendMode == other.multichannelBlendMode;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
    PathTraceSettings_V1, primaryStepSize, secondaryStepSize, multichannelBlendMode)
};
struct TimelineSettings_V1
{
  float minTime = 0;
  float maxTime = 0;
  float currentTime = 0;
  bool operator==(const TimelineSettings_V1& other) const
  {
    return minTime == other.minTime && maxTime == other.maxTime && currentTime == other.currentTime;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(TimelineSettings_V1, minTime, maxTime, currentTime)
};

enum class Projection_PID : int
{
  PERSPECTIVE = 0,
  ORTHOGRAPHIC = 1
};

struct CameraSettings_V1
{
  std::array<float, 3> eye = { 0, 0, -1 };
  std::array<float, 3> target = { 0, 0, 0 };
  std::array<float, 3> up = { 0, 1, 0 };
  Projection_PID projection = Projection_PID::PERSPECTIVE;
  float fovY = 55.0f;
  float orthoScale = 1.0f;
  float exposure = 0.75f;
  float aperture = 0.0f;
  float focalDistance = 1.0f;
  bool operator==(const CameraSettings_V1& other) const
  {
    return eye == other.eye && target == other.target && up == other.up && projection == other.projection &&
           fovY == other.fovY && orthoScale == other.orthoScale && exposure == other.exposure &&
           aperture == other.aperture && focalDistance == other.focalDistance;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(CameraSettings_V1,
                                 eye,
                                 target,
                                 up,
                                 projection,
                                 fovY,
                                 orthoScale,
                                 exposure,
                                 aperture,
                                 focalDistance)
};
struct ControlPointSettings_V1
{
  float x = 0.0f;
  std::array<float, 4> value = { 0.0f, 0.0f, 0.0f, 0.0f };

  bool operator==(const ControlPointSettings_V1& other) const { return x == other.x && value == other.value; }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(ControlPointSettings_V1, x, value)
};

enum class GradientEditMode_PID : int
{
  WINDOW_LEVEL = 0,
  ISOVALUE = 1,
  PERCENTILE = 2,
  CUSTOM = 3,
  MINMAX = 4
};

struct LutParams_V1
{
  float window = 0.5f;
  float level = 0.5f;
  float isovalue = 0.5f;
  float isorange = 0.01f;
  float pctLow = 0.5f;
  float pctHigh = 0.98f;
  uint16_t minu16 = 0;
  uint16_t maxu16 = 65535;
  std::vector<ControlPointSettings_V1> controlPoints;
  GradientEditMode_PID mode = GradientEditMode_PID::WINDOW_LEVEL;

  bool operator==(const LutParams_V1& other) const
  {
    return window == other.window && level == other.level && isovalue == other.isovalue && isorange == other.isorange &&
           pctLow == other.pctLow && pctHigh == other.pctHigh && controlPoints == other.controlPoints &&
           minu16 == other.minu16 && maxu16 == other.maxu16 && mode == other.mode;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LutParams_V1,
                                              window,
                                              level,
                                              isovalue,
                                              isorange,
                                              pctLow,
                                              pctHigh,
                                              controlPoints,
                                              mode,
                                              minu16,
                                              maxu16)
};

struct ColorMap
{
  std::string name = "none";
  std::vector<ControlPointSettings_V1> stops;
  bool operator==(const ColorMap& other) const { return name == other.name && stops == other.stops; }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ColorMap, name, stops)
};

struct ChannelSettings_V1
{
  bool enabled = true;
  std::array<float, 3> diffuseColor = { 1.0f, 1.0f, 1.0f };
  std::array<float, 3> specularColor = { 0.0f, 0.0f, 0.0f };
  std::array<float, 3> emissiveColor = { 0.0f, 0.0f, 0.0f };
  float glossiness = 0.0f;
  float opacity = 1.0f;
  LutParams_V1 lutParams;
  ColorMap colorMap;

  bool operator==(const ChannelSettings_V1& other) const
  {
    return enabled == other.enabled && diffuseColor == other.diffuseColor && specularColor == other.specularColor &&
           emissiveColor == other.emissiveColor && glossiness == other.glossiness && opacity == other.opacity &&
           lutParams == other.lutParams && colorMap == other.colorMap;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ChannelSettings_V1,
                                              enabled,
                                              diffuseColor,
                                              specularColor,
                                              emissiveColor,
                                              glossiness,
                                              opacity,
                                              lutParams,
                                              colorMap)
};

enum class LightType : int
{
  SKY = 1,
  AREA = 0
};

struct LightSettings_V1
{
  LightType type = LightType::SKY;
  float distance = 1.0f;
  float theta = 0.0f;
  float phi = 3.14159265358979323846f / 2.0f;
  std::array<float, 3> color = { 1.0f, 1.0f, 1.0f };
  float colorIntensity = 1.0f;
  std::array<float, 3> topColor = { 1.0f, 1.0f, 1.0f };
  float topColorIntensity = 1.0f;
  std::array<float, 3> middleColor = { 1.0f, 1.0f, 1.0f };
  float middleColorIntensity = 1.0f;
  std::array<float, 3> bottomColor = { 1.0f, 1.0f, 1.0f };
  float bottomColorIntensity = 1.0f;
  float width = 1.0f;
  float height = 1.0f;
  // quaternion in xyzw order; identity by default
  std::array<float, 4> rotation = { 0.0f, 0.0f, 0.0f, 1.0f };

  bool operator==(const LightSettings_V1& other) const
  {
    return type == other.type && distance == other.distance && theta == other.theta && phi == other.phi &&
           color == other.color && colorIntensity == other.colorIntensity && topColor == other.topColor &&
           topColorIntensity == other.topColorIntensity && middleColor == other.middleColor &&
           middleColorIntensity == other.middleColorIntensity && bottomColor == other.bottomColor &&
           bottomColorIntensity == other.bottomColorIntensity && width == other.width && height == other.height &&
           rotation == other.rotation;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LightSettings_V1,
                                              type,
                                              distance,
                                              theta,
                                              phi,
                                              color,
                                              colorIntensity,
                                              topColor,
                                              topColorIntensity,
                                              middleColor,
                                              middleColorIntensity,
                                              bottomColor,
                                              bottomColorIntensity,
                                              width,
                                              height,
                                              rotation)
};

struct ViewerState_V1
{
  std::string name; // m_volumeImageFile
  // the version of this schema
  // use app version
  std::array<uint32_t, 3> version{ 0, 0, 0 };
  std::array<int, 2> resolution = { 0, 0 }; // m_resolutionX, m_resolutionY
  int renderIterations = 1;                 // m_renderIterations

  PathTraceSettings_V1 pathTracer; // m_primaryStepSize, m_secondaryStepSize
  TimelineSettings_V1 timeline;    // m_minTime, m_maxTime, m_currentTime
  int scene = 0;                   // m_currentScene

  // [[xm, xM], [ym, yM], [zm, zM]]
  std::array<std::array<float, 2>, 3> clipRegion = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f };

  std::array<float, 3> scale = { 1, 1, 1 }; // m_scaleX, m_scaleY, m_scaleZ

  CameraSettings_V1 camera;

  std::array<float, 3> backgroundColor = { 0, 0, 0 }; // m_backgroundColor

  std::array<float, 3> boundingBoxColor = { 1, 1, 1 }; // m_boundingBoxColor

  bool showBoundingBox = false; // m_showBoundingBox

  std::vector<ChannelSettings_V1> channels; // m_channels

  float density = 50.0f;

  // lighting
  std::vector<LightSettings_V1> lights; // m_lights

  bool operator==(const ViewerState_V1& other) const
  {
    return name == other.name && version == other.version && resolution == other.resolution &&
           renderIterations == other.renderIterations && pathTracer == other.pathTracer && timeline == other.timeline &&
           scene == other.scene && clipRegion == other.clipRegion && scale == other.scale && camera == other.camera &&
           backgroundColor == other.backgroundColor && boundingBoxColor == other.boundingBoxColor &&
           showBoundingBox == other.showBoundingBox && channels == other.channels && density == other.density &&
           lights == other.lights;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(ViewerState_V1,
                                 name,
                                 version,
                                 resolution,
                                 renderIterations,
                                 pathTracer,
                                 timeline,
                                 scene,
                                 clipRegion,
                                 scale,
                                 camera,
                                 backgroundColor,
                                 boundingBoxColor,
                                 showBoundingBox,
                                 channels,
                                 density,
                                 lights)
};
} // namespace Serialize
