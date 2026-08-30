#pragma once

#include "io/FileReader.h"

#include <string>
#include <vector>

class CCamera;
class Renderer;
class RenderSettings;
class Scene;

class RendererCommandInterface
{
public:
  virtual void setStreamMode(int32_t mode) = 0;
  virtual void resizeGL(int x, int y) = 0;
};

class ParseableStream
{
public:
  virtual int32_t parseInt32() = 0;
  virtual float parseFloat32() = 0;
  virtual std::vector<float> parseFloat32Array() = 0;
  virtual std::vector<int32_t> parseInt32Array() = 0;
  virtual std::string parseString() = 0;
};

class WriteableStream
{
public:
  virtual size_t writeInt32(int32_t) = 0;
  virtual size_t writeFloat32(float) = 0;
  virtual size_t writeFloat32Array(const std::vector<float>&) = 0;
  virtual size_t writeInt32Array(const std::vector<int32_t>&) = 0;
  virtual size_t writeString(const std::string&) = 0;
};

enum class CommandArgType
{
  I32,
  F32,
  STR,
  F32A,
  I32A
};

struct ExecutionContext
{
  // we may need to reload data from the file again
  LoadSpec m_loadSpec;

  RendererCommandInterface* m_renderer;
  RenderSettings* m_renderSettings;
  Scene* m_appScene;
  CCamera* m_camera;
  std::string m_message;

  // Reader for the currently loaded file, kept so that stepping through time
  // does not reopen the file and re-parse its metadata for every time step.
  // The readers memoize per instance -- zarr its multiscale dims, czi its
  // subblock directory and metadata XML -- so a fresh reader per time step threw
  // all of that away, which is what SetTimeCommand used to do.
  //
  // Returns a reader for `spec`, reusing the cached one when it is for the same
  // file. Never null unless the format is unsupported.
  std::shared_ptr<IFileReader> readerFor(const LoadSpec& spec);
  void setReader(const LoadSpec& spec, std::shared_ptr<IFileReader> reader);

private:
  std::shared_ptr<IFileReader> m_reader;
  std::string m_readerPath;
  bool m_readerIsImageSequence = false;
};

class Command
{
public:
  virtual void execute(ExecutionContext* context) = 0;
  virtual std::string toPythonString() const = 0;
  virtual size_t write(WriteableStream* buffer) const = 0;

  virtual ~Command() {}
};

// commands should be named NameOfCommand
// and their parameters in a struct called NameOfCommandD (D for Data)
#define CMDDECL(NAME, CMDID, PYTHONNAME, ARGTYPES)                                                                     \
  class NAME : public Command                                                                                          \
  {                                                                                                                    \
  public:                                                                                                              \
    NAME(NAME##D d)                                                                                                    \
      : m_data(d)                                                                                                      \
    {                                                                                                                  \
    }                                                                                                                  \
    void execute(ExecutionContext* context) override;                                                                  \
    std::string toPythonString() const override;                                                                       \
    size_t write(WriteableStream* buffer) const override;                                                              \
    static NAME* parse(ParseableStream* buffer);                                                                       \
    static const uint32_t m_ID = CMDID;                                                                                \
    static const std::string PythonName()                                                                              \
    {                                                                                                                  \
      static const std::string s = PYTHONNAME;                                                                         \
      return s;                                                                                                        \
    }                                                                                                                  \
    static const std::vector<CommandArgType> ArgTypes()                                                                \
    {                                                                                                                  \
      static const std::vector<CommandArgType> a = ARGTYPES;                                                           \
      return a;                                                                                                        \
    }                                                                                                                  \
    NAME##D m_data;                                                                                                    \
  };

#define CMD_ARGS(...) __VA_ARGS__

struct SessionCommandD
{
  std::string m_name;
};
CMDDECL(SessionCommand, 0, "session", CMD_ARGS({ CommandArgType::STR }));

struct AssetPathCommandD
{
  std::string m_name;
};
CMDDECL(AssetPathCommand, 1, "asset_path", CMD_ARGS({ CommandArgType::STR }));

struct LoadOmeTifCommandD
{
  std::string m_name;
};
CMDDECL(LoadOmeTifCommand, 2, "load_ome_tif", CMD_ARGS({ CommandArgType::STR }));

struct SetCameraPosCommandD
{
  float m_x, m_y, m_z;
};
CMDDECL(SetCameraPosCommand, 3, "eye", CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetCameraTargetCommandD
{
  float m_x, m_y, m_z;
};
CMDDECL(SetCameraTargetCommand,
        4,
        "target",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetCameraUpCommandD
{
  float m_x, m_y, m_z;
};
CMDDECL(SetCameraUpCommand, 5, "up", CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetCameraApertureCommandD
{
  float m_x;
};
CMDDECL(SetCameraApertureCommand, 6, "aperture", CMD_ARGS({ CommandArgType::F32 }));

struct SetCameraProjectionCommandD
{
  // perspective or ortho
  int32_t m_projectionType;
  // fov degrees or ortho scale
  float m_x;
};
CMDDECL(SetCameraProjectionCommand, 7, "camera_projection", CMD_ARGS({ CommandArgType::I32, CommandArgType::F32 }));

struct SetCameraFocalDistanceCommandD
{
  float m_x;
};
CMDDECL(SetCameraFocalDistanceCommand, 8, "focaldist", CMD_ARGS({ CommandArgType::F32 }));

struct SetCameraExposureCommandD
{
  float m_x;
};
CMDDECL(SetCameraExposureCommand, 9, "exposure", CMD_ARGS({ CommandArgType::F32 }));

struct SetDiffuseColorCommandD
{
  int32_t m_channel;
  float m_r, m_g, m_b, m_a;
};
CMDDECL(SetDiffuseColorCommand,
        10,
        "mat_diffuse",
        CMD_ARGS(
          { CommandArgType::I32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetSpecularColorCommandD
{
  int32_t m_channel;
  float m_r, m_g, m_b, m_a;
};
CMDDECL(SetSpecularColorCommand,
        11,
        "mat_specular",
        CMD_ARGS(
          { CommandArgType::I32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetEmissiveColorCommandD
{
  int32_t m_channel;
  float m_r, m_g, m_b, m_a;
};
CMDDECL(SetEmissiveColorCommand,
        12,
        "mat_emissive",
        CMD_ARGS(
          { CommandArgType::I32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetRenderIterationsCommandD
{
  int32_t m_x;
};
CMDDECL(SetRenderIterationsCommand, 13, "render_iterations", CMD_ARGS({ CommandArgType::I32 }));

struct SetStreamModeCommandD
{
  int32_t m_x;
};
CMDDECL(SetStreamModeCommand, 14, "stream_mode", CMD_ARGS({ CommandArgType::I32 }));

struct RequestRedrawCommandD
{};
CMDDECL(RequestRedrawCommand, 15, "redraw", CMD_ARGS({}));

struct SetResolutionCommandD
{
  int32_t m_x, m_y;
};
CMDDECL(SetResolutionCommand, 16, "set_resolution", CMD_ARGS({ CommandArgType::I32, CommandArgType::I32 }));

struct SetDensityCommandD
{
  float m_x;
};
CMDDECL(SetDensityCommand, 17, "density", CMD_ARGS({ CommandArgType::F32 }));

struct FrameSceneCommandD
{};
CMDDECL(FrameSceneCommand, 18, "frame_scene", CMD_ARGS({}));

struct SetGlossinessCommandD
{
  int32_t m_channel;
  float m_glossiness;
};
CMDDECL(SetGlossinessCommand, 19, "mat_glossiness", CMD_ARGS({ CommandArgType::I32, CommandArgType::F32 }));

struct EnableChannelCommandD
{
  int32_t m_channel;
  int32_t m_enabled;
};
CMDDECL(EnableChannelCommand, 20, "enable_channel", CMD_ARGS({ CommandArgType::I32, CommandArgType::I32 }));

struct SetWindowLevelCommandD
{
  int32_t m_channel;
  float m_window;
  float m_level;
};
CMDDECL(SetWindowLevelCommand,
        21,
        "set_window_level",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32 }));

struct OrbitCameraCommandD
{
  float m_theta;
  float m_phi;
};
CMDDECL(OrbitCameraCommand, 22, "orbit_camera", CMD_ARGS({ CommandArgType::F32, CommandArgType::F32 }));

struct SetSkylightTopColorCommandD
{
  float m_r, m_g, m_b;
};
CMDDECL(SetSkylightTopColorCommand,
        23,
        "skylight_top_color",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetSkylightMiddleColorCommandD
{
  float m_r, m_g, m_b;
};
CMDDECL(SetSkylightMiddleColorCommand,
        24,
        "skylight_middle_color",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetSkylightBottomColorCommandD
{
  float m_r, m_g, m_b;
};
CMDDECL(SetSkylightBottomColorCommand,
        25,
        "skylight_bottom_color",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetLightPosCommandD
{
  int32_t m_index;
  float m_r, m_theta, m_phi;
};
CMDDECL(SetLightPosCommand,
        26,
        "light_pos",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetLightColorCommandD
{
  int32_t m_index;
  float m_r, m_g, m_b;
};
CMDDECL(SetLightColorCommand,
        27,
        "light_color",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetLightSizeCommandD
{
  int32_t m_index;
  float m_x, m_y;
};
CMDDECL(SetLightSizeCommand,
        28,
        "light_size",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32 }));

struct SetClipRegionCommandD
{
  float m_minx, m_maxx;
  float m_miny, m_maxy;
  float m_minz, m_maxz;
};
CMDDECL(SetClipRegionCommand,
        29,
        "set_clip_region",
        CMD_ARGS({ CommandArgType::F32,
                   CommandArgType::F32,
                   CommandArgType::F32,
                   CommandArgType::F32,
                   CommandArgType::F32,
                   CommandArgType::F32 }));

struct SetVoxelScaleCommandD
{
  float m_x, m_y, m_z;
};
CMDDECL(SetVoxelScaleCommand,
        30,
        "set_voxel_scale",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct AutoThresholdCommandD
{
  int32_t m_channel;
  int32_t m_method;
};
CMDDECL(AutoThresholdCommand, 31, "auto_threshold", CMD_ARGS({ CommandArgType::I32, CommandArgType::I32 }));

struct SetPercentileThresholdCommandD
{
  int32_t m_channel;
  float m_pctLow;
  float m_pctHigh;
};
CMDDECL(SetPercentileThresholdCommand,
        32,
        "set_percentile_threshold",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32 }));

struct SetOpacityCommandD
{
  int32_t m_channel;
  float m_opacity;
};
CMDDECL(SetOpacityCommand, 33, "mat_opacity", CMD_ARGS({ CommandArgType::I32, CommandArgType::F32 }));

struct SetPrimaryRayStepSizeCommandD
{
  float m_stepSize;
};
CMDDECL(SetPrimaryRayStepSizeCommand, 34, "set_primary_ray_step_size", CMD_ARGS({ CommandArgType::F32 }));

struct SetSecondaryRayStepSizeCommandD
{
  float m_stepSize;
};
CMDDECL(SetSecondaryRayStepSizeCommand, 35, "set_secondary_ray_step_size", CMD_ARGS({ CommandArgType::F32 }));

struct SetBackgroundColorCommandD
{
  float m_r, m_g, m_b;
};
CMDDECL(SetBackgroundColorCommand,
        36,
        "background_color",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetIsovalueThresholdCommandD
{
  int32_t m_channel;
  float m_isovalue;
  float m_isorange;
};
CMDDECL(SetIsovalueThresholdCommand,
        37,
        "set_isovalue_threshold",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::F32, CommandArgType::F32 }));

struct SetControlPointsCommandD
{
  int32_t m_channel;
  std::vector<float> m_data;
};
CMDDECL(SetControlPointsCommand, 38, "set_control_points", CMD_ARGS({ CommandArgType::I32, CommandArgType::F32A }));

struct LoadVolumeFromFileCommandD
{
  std::string m_path;
  int32_t m_scene;
  int32_t m_time;
};
CMDDECL(LoadVolumeFromFileCommand,
        39,
        "load_volume_from_file",
        CMD_ARGS({ CommandArgType::STR, CommandArgType::I32, CommandArgType::I32 }));

struct SetTimeCommandD
{
  int32_t m_time;
};
CMDDECL(SetTimeCommand, 40, "set_time", CMD_ARGS({ CommandArgType::I32 }));

struct SetBoundingBoxColorCommandD
{
  float m_r, m_g, m_b;
};
CMDDECL(SetBoundingBoxColorCommand,
        41,
        "bounding_box_color",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct ShowBoundingBoxCommandD
{
  int32_t m_on;
};
CMDDECL(ShowBoundingBoxCommand, 42, "show_bounding_box", CMD_ARGS({ CommandArgType::I32 }));

struct TrackballCameraCommandD
{
  float m_theta;
  float m_phi;
};
CMDDECL(TrackballCameraCommand, 43, "trackball_camera", CMD_ARGS({ CommandArgType::F32, CommandArgType::F32 }));

struct LoadDataCommandD
{
  std::string m_path;
  int32_t m_scene;
  int32_t m_level;
  int32_t m_time;
  std::vector<int32_t> m_channels;
  int32_t m_xmin, m_xmax;
  int32_t m_ymin, m_ymax;
  int32_t m_zmin, m_zmax;
};

CMDDECL(LoadDataCommand,
        44,
        "load_data",
        CMD_ARGS({ CommandArgType::STR,
                   CommandArgType::I32,
                   CommandArgType::I32,
                   CommandArgType::I32,
                   CommandArgType::I32A,
                   CommandArgType::I32A }));

struct ShowScaleBarCommandD
{
  int32_t m_on;
};
CMDDECL(ShowScaleBarCommand, 45, "show_scale_bar", CMD_ARGS({ CommandArgType::I32 }));

struct SetFlipAxisCommandD
{
  int32_t m_x, m_y, m_z;
};
CMDDECL(SetFlipAxisCommand,
        46,
        "set_flip_axis",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::I32, CommandArgType::I32 }));

struct SetInterpolationCommandD
{
  int32_t m_on;
};
CMDDECL(SetInterpolationCommand, 47, "set_interpolation", CMD_ARGS({ CommandArgType::I32 }));

struct SetClipPlaneCommandD
{
  float m_x, m_y, m_z, m_w;
};
CMDDECL(SetClipPlaneCommand,
        48,
        "set_clip_plane",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));

struct SetColorRampCommandD
{
  int32_t m_channel;
  std::string m_name;
  std::vector<float> m_data;
};
CMDDECL(SetColorRampCommand,
        49,
        "set_color_ramp",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::STR, CommandArgType::F32A }));

struct SetMinMaxThresholdCommandD
{
  int32_t m_channel;
  int32_t m_min;
  int32_t m_max;
};
CMDDECL(SetMinMaxThresholdCommand,
        50,
        "set_min_max_threshold",
        CMD_ARGS({ CommandArgType::I32, CommandArgType::I32, CommandArgType::I32 }));

struct SetSkylightRotationCommandD
{
  float m_x, m_y, m_z, m_w;
};
CMDDECL(SetSkylightRotationCommand,
        51,
        "set_skylight_rotation",
        CMD_ARGS({ CommandArgType::F32, CommandArgType::F32, CommandArgType::F32, CommandArgType::F32 }));
struct ShowTimeStampCommandD
{
  int32_t m_on;
};
CMDDECL(ShowTimeStampCommand, 52, "show_time_stamp", CMD_ARGS({ CommandArgType::I32 }));

struct SetTimeStampFormatCommandD
{
  int32_t m_format;
};
CMDDECL(SetTimeStampFormatCommand, 53, "set_time_stamp_format", CMD_ARGS({ CommandArgType::I32 }));

struct SetMultichannelBlendCommandD
{
  int32_t m_mode;
};
CMDDECL(SetMultichannelBlendCommand, 54, "set_multichannel_blend", CMD_ARGS({ CommandArgType::I32 }));
