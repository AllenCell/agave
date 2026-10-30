#pragma once

#include "SerializeV1.h"
#include "renderlib/json/json.hpp"

#include <array>
#include <string>
#include <vector>

namespace Serialize {

enum class RendererType_PID : int
{
  PATHTRACE = 0,
  RAYMARCH = 1,
  SLICE_Z = 2,
  SLICE_Y = 3,
  SLICE_X = 4,
  TRIPLE = 5
};

struct LoadSettings
{
  std::string url;
  std::string subpath;
  bool isImageSequence = false;
  uint32_t scene = 0;
  uint32_t time = 0;
  std::vector<uint32_t> channels;
  std::array<std::array<uint32_t, 2>, 3> clipRegion = { 0, 0, 0, 0, 0, 0 };

  bool operator==(const LoadSettings& other) const
  {
    return url == other.url && subpath == other.subpath && isImageSequence == other.isImageSequence &&
           scene == other.scene && time == other.time && channels == other.channels && clipRegion == other.clipRegion;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoadSettings,
                                              url,
                                              subpath,
                                              isImageSequence,
                                              scene,
                                              time,
                                              channels,
                                              clipRegion)
};

struct Transform
{
  std::array<float, 3> translation = { 0, 0, 0 };
  std::array<float, 4> rotation = { 0, 0, 0, 0 }; // quaternion in xyzw order
  std::array<float, 3> scale = { 1, 1, 1 };

  bool operator==(const Transform& other) const
  {
    return translation == other.translation && rotation == other.rotation && scale == other.scale;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Transform, translation, rotation, scale)
};

enum class DurationType_PID : int
{
  SAMPLES = 0,
  TIME = 1
};

struct CaptureSettings
{
  int width = 960;
  int height = 540;
  std::string filenamePrefix = "frame";
  std::string outputDirectory = ".";
  int samples = 32;
  float seconds = 10.0f;
  DurationType_PID durationType = DurationType_PID::SAMPLES;
  int startTime = 0;
  int endTime = 0;

  bool operator==(const CaptureSettings& other) const
  {
    return width == other.width && height == other.height && filenamePrefix == other.filenamePrefix &&
           outputDirectory == other.outputDirectory && samples == other.samples && seconds == other.seconds &&
           durationType == other.durationType && startTime == other.startTime && endTime == other.endTime;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(CaptureSettings,
                                 width,
                                 height,
                                 filenamePrefix,
                                 outputDirectory,
                                 samples,
                                 seconds,
                                 durationType,
                                 startTime,
                                 endTime)
};

struct ClipPlane
{
  Transform transform;
  std::array<float, 4> clipPlane = { 0, 0, 0, 0 };
  bool enabled = false;

  bool operator==(const ClipPlane& other) const
  {
    return clipPlane == other.clipPlane && transform == other.transform && enabled == other.enabled;
  }

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipPlane, clipPlane, transform, enabled)
};

struct ViewerState
{
  std::vector<LoadSettings> datasets;

  // the app version that wrote this data
  std::array<uint32_t, 3> version{ 0, 0, 0 };

  RendererType_PID rendererType = RendererType_PID::PATHTRACE;
  // Current X/Y/Z slice indices. These are retained for all render modes so
  // switching between the 2D views restores the last slice on each axis.
  std::array<uint32_t, 3> sliceIndices = { 0, 0, 0 };
  PathTraceSettings_V1 pathTracer; // m_primaryStepSize, m_secondaryStepSize
  TimelineSettings_V1 timeline;    // m_minTime, m_maxTime, m_currentTime

  // [[xm, xM], [ym, yM], [zm, zM]]
  std::array<std::array<float, 2>, 3> clipRegion = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f };
  ClipPlane clipPlane;

  std::array<float, 3> scale = { 1, 1, 1 };  // m_scaleX, m_scaleY, m_scaleZ
  std::array<int, 3> flipAxis = { 1, 1, 1 }; // 1 is unflipped, -1 is flipped

  CameraSettings_V1 camera;

  std::array<float, 3> backgroundColor = { 0, 0, 0 }; // m_backgroundColor

  std::array<float, 3> boundingBoxColor = { 1, 1, 1 }; // m_boundingBoxColor

  bool showBoundingBox = false; // m_showBoundingBox
  bool showScaleBar = false;
  bool showTimeStamp = false;
  int32_t timeStampFormat = 1; // 0=HH:MM:SS, 1=Time Units

  std::vector<ChannelSettings_V1> channels; // m_channels

  float density = 50.0f;
  bool interpolate = true;

  // lighting
  std::vector<LightSettings_V1> lights; // m_lights

  CaptureSettings capture;

  bool operator==(const ViewerState& other) const
  {
    return datasets == other.datasets && version == other.version && rendererType == other.rendererType &&
           sliceIndices == other.sliceIndices && pathTracer == other.pathTracer &&
           timeline == other.timeline && clipRegion == other.clipRegion && clipPlane == other.clipPlane &&
           scale == other.scale && flipAxis == other.flipAxis && camera == other.camera &&
           backgroundColor == other.backgroundColor && boundingBoxColor == other.boundingBoxColor &&
           showBoundingBox == other.showBoundingBox && showScaleBar == other.showScaleBar &&
           showTimeStamp == other.showTimeStamp && timeStampFormat == other.timeStampFormat &&
           channels == other.channels && density == other.density && lights == other.lights &&
           capture == other.capture && interpolate == other.interpolate;
  }
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ViewerState,
                                              datasets,
                                              version,
                                              rendererType,
                                              sliceIndices,
                                              pathTracer,
                                              timeline,
                                              clipRegion,
                                              clipPlane,
                                              scale,
                                              flipAxis,
                                              camera,
                                              backgroundColor,
                                              boundingBoxColor,
                                              showBoundingBox,
                                              channels,
                                              density,
                                              interpolate,
                                              lights,
                                              capture,
                                              showScaleBar,
                                              showTimeStamp,
                                              timeStampFormat)
};

ViewerState
fromV1(const ViewerState_V1& v1);

} // namespace Serialize
