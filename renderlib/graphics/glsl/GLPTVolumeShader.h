#pragma once

#include "gfxOpenGL/Backend.h"

#include "CCamera.h"
#include "gl/Util.h"

#include <glm.h>

#define MAX_GL_CHANNELS 4

class DenoiseParams;
struct PathTraceRenderSettings;
struct ImageGpu;
class Scene;

class GLPTVolumeShader : public GLShaderProgram
{

public:
  /**
   * Constructor.
   *
   * @param parent the parent of this object.
   */
  explicit GLPTVolumeShader();

  /// Destructor.
  ~GLPTVolumeShader();

  void setTransformUniforms(const CCamera& camera, const glm::mat4& modelMatrix);

  void setShadingUniforms(const Scene* scene,
                          const DenoiseParams& denoise,
                          const CCamera& cam,
                          const CBoundingBox& clipped_bbox,
                          const PathTraceRenderSettings& renderSettings,
                          int numIterations,
                          int randSeed,
                          int w,
                          int h,
                          const ImageGpu& imggpu,
                          GLuint accumulationTexture);

private:
  /// The vertex shader.
  GLShader* m_vshader;
  /// The fragment shader.
  GLShader* m_fshader;

  int m_volumeTexture;
  int m_tPreviousTexture, m_uSampleCounter, m_uFrameCounter, m_uResolution, m_gClippedAaBbMin, m_gClippedAaBbMax,
    m_gDensityScale, m_gStepSize, m_gStepSizeShadow, m_gPosToUVW, m_g_nChannels, m_gShadingType, m_gGradientDeltaX,
    m_gGradientDeltaY, m_gGradientDeltaZ, m_gInvGradientDelta, m_gGradientFactor;
  int m_cameraFrom, m_cameraU, m_cameraV, m_cameraN, m_cameraScreen, m_cameraInvScreen, m_cameraFocalDistance,
    m_cameraApertureSize, m_cameraProjectionMode;

  int m_light0theta, m_light0phi, m_light0width, m_light0halfWidth, m_light0height, m_light0halfHeight,
    m_light0distance, m_light0skyRadius, m_light0P, m_light0target, m_light0N, m_light0U, m_light0V, m_light0area,
    m_light0areaPdf, m_light0color, m_light0colorTop, m_light0colorMiddle, m_light0colorBottom, m_light0T;
  int m_light1theta, m_light1phi, m_light1width, m_light1halfWidth, m_light1height, m_light1halfHeight,
    m_light1distance, m_light1skyRadius, m_light1P, m_light1target, m_light1N, m_light1U, m_light1V, m_light1area,
    m_light1areaPdf, m_light1color, m_light1colorTop, m_light1colorMiddle, m_light1colorBottom, m_light1T;

  int m_lutTexture0, m_lutTexture1, m_lutTexture2, m_lutTexture3, m_tf, m_tf_nNodes, m_colormapTexture, m_intensityMax,
    m_intensityMin, m_lutMax, m_lutMin, m_labels, m_opacity, m_emissive0, m_emissive1, m_emissive2, m_emissive3,
    m_diffuse0, m_diffuse1, m_diffuse2, m_diffuse3, m_specular0, m_specular1, m_specular2, m_specular3, m_roughness,
    m_uShowLights;
  int m_clipPlane;
};
