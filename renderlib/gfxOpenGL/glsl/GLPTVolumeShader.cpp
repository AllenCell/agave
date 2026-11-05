#include "GLPTVolumeShader.h"

#include "AppScene.h"
#include "BoundingBox.h"
#include "CCamera.h"
#include "DenoiseParams.h"
#include "ImageXYZC.h"
#include "gfxOpenGL/ImageXyzcGpu.h"
#include "Logging.h"
#include "shaders.h"

#include "gfxOpenGL/Util.h"
#include <glm.h>

#include <iostream>
#include <sstream>

GLPTVolumeShader::GLPTVolumeShader()
  : GLShaderProgram()
  , m_vshader()
  , m_fshader()
{
  m_vshader = new GLShader(GL_VERTEX_SHADER);
  m_vshader->compileSourceCode(getShaderSource("pathTraceVolume_vert").c_str());

  if (!m_vshader->isCompiled()) {
    LOG_ERROR << "GLPTVolumeShader: Failed to compile vertex shader\n" << m_vshader->log();
  }

  m_fshader = new GLShader(GL_FRAGMENT_SHADER);

  m_fshader->compileSourceCode(getShaderSource("pathTraceVolume_frag").c_str());
  if (!m_fshader->isCompiled()) {
    LOG_ERROR << "GLPTVolumeShader: Failed to compile fragment shader\n" << m_fshader->log();
  }

  addShader(m_vshader);
  addShader(m_fshader);
  link();

  if (!isLinked()) {
    LOG_ERROR << "GLPTVolumeShader: Failed to link shader program\n" << log();
  }

  m_volumeTexture = uniformLocation("volumeTexture");

  m_tPreviousTexture = uniformLocation("tPreviousTexture");
  m_uSampleCounter = uniformLocation("uSampleCounter"); // 0
  m_uFrameCounter = uniformLocation("uFrameCounter");   // 1

  m_uResolution = uniformLocation("uResolution"); // : { type : "v2", value : new THREE.Vector2() },

  ///////////////////////////
  m_gClippedAaBbMin = uniformLocation("gClippedAaBbMin"); // : { type : "v3", value : new THREE.Vector3(0, 0, 0) },
  m_gClippedAaBbMax = uniformLocation("gClippedAaBbMax"); // : { type : "v3", value : new THREE.Vector3(1, 1, 1) },
  m_gDensityScale = uniformLocation("gDensityScale");     // : { type : "f", value : 50.0 },
  m_gStepSize = uniformLocation("gStepSize");             // : { type : "f", value : 1.0 },
  m_gStepSizeShadow = uniformLocation("gStepSizeShadow"); // : { type : "f", value : 1.0 },
  m_gPosToUVW = uniformLocation("gPosToUVW");             // : { type : "v3", value : new THREE.Vector3() },
  m_g_nChannels = uniformLocation("g_nChannels");         // : { type : "i", value : 1 },
  m_gShadingType = uniformLocation("gShadingType");       // : { type : "i", value : 2 },
  m_gGradientDeltaX = uniformLocation("gGradientDeltaX"); // : { type : "v3", value : new THREE.Vector3(0.01, 0, 0) },
  m_gGradientDeltaY = uniformLocation("gGradientDeltaY"); // : { type : "v3", value : new THREE.Vector3(0, 0.01, 0) },
  m_gGradientDeltaZ = uniformLocation("gGradientDeltaZ"); // : { type : "v3", value : new THREE.Vector3(0, 0, 0.01) },
  m_gInvGradientDelta = uniformLocation("gInvGradientDelta"); // : { type : "f", value : 0.0 },
  m_gGradientFactor = uniformLocation("gGradientFactor");     // : { type : "f", value : 0.5 },

  m_cameraFrom = uniformLocation("gCamera.m_from");
  m_cameraU = uniformLocation("gCamera.m_U");
  m_cameraV = uniformLocation("gCamera.m_V");
  m_cameraN = uniformLocation("gCamera.m_N");
  m_cameraScreen = uniformLocation("gCamera.m_screen");
  m_cameraInvScreen = uniformLocation("gCamera.m_invScreen");
  m_cameraFocalDistance = uniformLocation("gCamera.m_focalDistance");
  m_cameraApertureSize = uniformLocation("gCamera.m_apertureSize");
  m_cameraProjectionMode = uniformLocation("gCamera.m_isPerspective");

  // Camera struct
  //          m_from : new THREE.Vector3(),
  //          m_U : new THREE.Vector3(),
  //          m_V : new THREE.Vector3(),
  //          m_N : new THREE.Vector3(),
  //          m_screen : new THREE.Vector4(),    // left, right, bottom, top
  //          m_invScreen : new THREE.Vector2(), // 1/w, 1/h
  //          m_focalDistance : 0.0,
  //          m_apertureSize : 0.0

  m_light0theta = uniformLocation("gLights[0].m_theta");
  m_light0phi = uniformLocation("gLights[0].m_phi");
  m_light0width = uniformLocation("gLights[0].m_width");
  m_light0halfWidth = uniformLocation("gLights[0].m_halfWidth");
  m_light0height = uniformLocation("gLights[0].m_height");
  m_light0halfHeight = uniformLocation("gLights[0].m_halfHeight");
  m_light0distance = uniformLocation("gLights[0].m_distance");
  m_light0skyRadius = uniformLocation("gLights[0].m_skyRadius");
  m_light0P = uniformLocation("gLights[0].m_P");
  m_light0target = uniformLocation("gLights[0].m_target");
  m_light0N = uniformLocation("gLights[0].m_N");
  m_light0U = uniformLocation("gLights[0].m_U");
  m_light0V = uniformLocation("gLights[0].m_V");
  m_light0area = uniformLocation("gLights[0].m_area");
  m_light0areaPdf = uniformLocation("gLights[0].m_areaPdf");
  m_light0color = uniformLocation("gLights[0].m_color");
  m_light0colorTop = uniformLocation("gLights[0].m_colorTop");
  m_light0colorMiddle = uniformLocation("gLights[0].m_colorMiddle");
  m_light0colorBottom = uniformLocation("gLights[0].m_colorBottom");
  m_light0T = uniformLocation("gLights[0].m_T");

  m_light1theta = uniformLocation("gLights[1].m_theta");
  m_light1phi = uniformLocation("gLights[1].m_phi");
  m_light1width = uniformLocation("gLights[1].m_width");
  m_light1halfWidth = uniformLocation("gLights[1].m_halfWidth");
  m_light1height = uniformLocation("gLights[1].m_height");
  m_light1halfHeight = uniformLocation("gLights[1].m_halfHeight");
  m_light1distance = uniformLocation("gLights[1].m_distance");
  m_light1skyRadius = uniformLocation("gLights[1].m_skyRadius");
  m_light1P = uniformLocation("gLights[1].m_P");
  m_light1target = uniformLocation("gLights[1].m_target");
  m_light1N = uniformLocation("gLights[1].m_N");
  m_light1U = uniformLocation("gLights[1].m_U");
  m_light1V = uniformLocation("gLights[1].m_V");
  m_light1area = uniformLocation("gLights[1].m_area");
  m_light1areaPdf = uniformLocation("gLights[1].m_areaPdf");
  m_light1color = uniformLocation("gLights[1].m_color");
  m_light1colorTop = uniformLocation("gLights[1].m_colorTop");
  m_light1colorMiddle = uniformLocation("gLights[1].m_colorMiddle");
  m_light1colorBottom = uniformLocation("gLights[1].m_colorBottom");
  m_light1T = uniformLocation("gLights[1].m_T");

  // per channel

  m_lutTexture0 = uniformLocation("g_lutTexture[0]");
  m_lutTexture1 = uniformLocation("g_lutTexture[1]");
  m_lutTexture2 = uniformLocation("g_lutTexture[2]");
  m_lutTexture3 = uniformLocation("g_lutTexture[3]");

  m_tf = uniformLocation("g_tf");
  m_tf_nNodes = uniformLocation("g_tf_nNodes");

  m_colormapTexture = uniformLocation("g_colormapTexture");
  m_intensityMax = uniformLocation("g_intensityMax");
  m_intensityMin = uniformLocation("g_intensityMin");
  m_lutMax = uniformLocation("g_lutMax");
  m_lutMin = uniformLocation("g_lutMin");
  m_labels = uniformLocation("g_labels");
  m_opacity = uniformLocation("g_opacity");
  m_emissive0 = uniformLocation("g_emissive[0]");
  m_emissive1 = uniformLocation("g_emissive[1]");
  m_emissive2 = uniformLocation("g_emissive[2]");
  m_emissive3 = uniformLocation("g_emissive[3]");
  m_diffuse0 = uniformLocation("g_diffuse[0]");
  m_diffuse1 = uniformLocation("g_diffuse[1]");
  m_diffuse2 = uniformLocation("g_diffuse[2]");
  m_diffuse3 = uniformLocation("g_diffuse[3]");
  m_specular0 = uniformLocation("g_specular[0]");
  m_specular1 = uniformLocation("g_specular[1]");
  m_specular2 = uniformLocation("g_specular[2]");
  m_specular3 = uniformLocation("g_specular[3]");
  m_roughness = uniformLocation("g_roughness");
  m_uShowLights = uniformLocation("uShowLights");
  m_clipPlane = uniformLocation("g_clipPlane");
}

GLPTVolumeShader::~GLPTVolumeShader() {}

void
GLPTVolumeShader::setShadingUniforms(const Scene* scene,
                                     const DenoiseParams& denoise,
                                     const CCamera& cam,
                                     const CBoundingBox& clipped_bbox,
                                     const PathTraceRenderSettings& renderSettings,
                                     int numIterations,
                                     int randSeed,
                                     int w,
                                     int h,
                                     const ImageGpu& imggpu,
                                     GLuint accumulationTexture)
{
  check_gl("before pathtrace shader uniform binding");

  glUniform1i(m_volumeTexture, 0);
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, imggpu.m_VolumeGLTexture);
  check_gl("post vol textures");

  glUniform1i(m_tPreviousTexture, 1);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, accumulationTexture);
  check_gl("post accum textures");

  glUniform1f(m_uSampleCounter, (float)numIterations);
  glUniform1f(m_uFrameCounter, (float)(randSeed + 1));
  // glUniform1f(m_uFrameCounter, 1.0f);

  glUniform2f(m_uResolution, (float)w, (float)h);
  glUniform3fv(m_gClippedAaBbMax, 1, glm::value_ptr(clipped_bbox.GetMaxP()));
  glUniform3fv(m_gClippedAaBbMin, 1, glm::value_ptr(clipped_bbox.GetMinP()));

  ///////////////////////////
  glUniform1f(m_gDensityScale, renderSettings.m_DensityScale);
  glUniform1f(m_gStepSize, renderSettings.m_StepSizeFactor * renderSettings.m_GradientDelta);
  glUniform1f(m_gStepSizeShadow, renderSettings.m_StepSizeFactorShadow * renderSettings.m_GradientDelta);
  glUniform3fv(
    m_gPosToUVW,
    1,
    glm::value_ptr(scene->m_boundingBox.GetInverseExtent() * glm::vec3(scene->m_volume->getVolumeAxesFlipped())));

  glUniform1i(m_gShadingType, renderSettings.m_ShadingType);

  const float GradientDelta = 1.0f * renderSettings.m_GradientDelta;
  const float invGradientDelta = 1.0f / GradientDelta;
  const glm::vec3 GradientDeltaX(GradientDelta, 0.0f, 0.0f);
  const glm::vec3 GradientDeltaY(0.0f, GradientDelta, 0.0f);
  const glm::vec3 GradientDeltaZ(0.0f, 0.0f, GradientDelta);

  glUniform3fv(m_gGradientDeltaX, 1, glm::value_ptr(GradientDeltaX));
  glUniform3fv(m_gGradientDeltaY, 1, glm::value_ptr(GradientDeltaY));
  glUniform3fv(m_gGradientDeltaZ, 1, glm::value_ptr(GradientDeltaZ));
  glUniform1f(m_gInvGradientDelta, invGradientDelta);
  glUniform1f(m_gGradientFactor, renderSettings.m_GradientFactor);

  glUniform3fv(m_cameraFrom, 1, glm::value_ptr(cam.m_From));
  glUniform3fv(m_cameraU, 1, glm::value_ptr(cam.m_U));
  glUniform3fv(m_cameraV, 1, glm::value_ptr(cam.m_V));
  glUniform3fv(m_cameraN, 1, glm::value_ptr(cam.m_N));
  glUniform4fv(m_cameraScreen, 1, (float*)cam.m_Film.m_Screen);
  glUniform2fv(m_cameraInvScreen, 1, glm::value_ptr(cam.m_Film.m_InvScreen));
  glUniform1f(m_cameraFocalDistance, cam.m_Focus.m_FocalDistance);
  glUniform1f(m_cameraApertureSize, cam.m_Aperture.m_Size);
  glUniform1f(m_cameraProjectionMode, (cam.m_Projection == PERSPECTIVE) ? 1.0f : 0.0f);
  check_gl("pre lights");

  Light& l = scene->SphereLight();
  glUniform1f(m_light0theta, l.m_Theta);
  glUniform1f(m_light0phi, l.m_Phi);
  glUniform1f(m_light0width, l.m_Width);
  glUniform1f(m_light0halfWidth, l.m_HalfWidth);
  glUniform1f(m_light0height, l.m_Height);
  glUniform1f(m_light0halfHeight, l.m_HalfHeight);
  glUniform1f(m_light0distance, l.m_Distance);
  glUniform1f(m_light0skyRadius, l.m_SkyRadius);
  glUniform3fv(m_light0P, 1, glm::value_ptr(l.m_P));
  glUniform3fv(m_light0target, 1, glm::value_ptr(l.m_Target));
  glUniform3fv(m_light0N, 1, glm::value_ptr(l.m_N));
  glUniform3fv(m_light0U, 1, glm::value_ptr(l.m_U));
  glUniform3fv(m_light0V, 1, glm::value_ptr(l.m_V));
  glUniform1f(m_light0area, l.m_Area);
  glUniform1f(m_light0areaPdf, l.m_AreaPdf);
  glUniform3fv(m_light0color, 1, glm::value_ptr(l.m_Color * l.m_ColorIntensity));
  glUniform3fv(m_light0colorTop, 1, glm::value_ptr(l.m_ColorTop * l.m_ColorTopIntensity));
  glUniform3fv(m_light0colorMiddle, 1, glm::value_ptr(l.m_ColorMiddle * l.m_ColorMiddleIntensity));
  glUniform3fv(m_light0colorBottom, 1, glm::value_ptr(l.m_ColorBottom * l.m_ColorBottomIntensity));
  glUniform1i(m_light0T, l.m_T);

  Light& l1 = scene->AreaLight();
  glUniform1f(m_light1theta, l1.m_Theta);
  glUniform1f(m_light1phi, l1.m_Phi);
  glUniform1f(m_light1width, l1.m_Width);
  glUniform1f(m_light1halfWidth, l1.m_HalfWidth);
  glUniform1f(m_light1height, l1.m_Height);
  glUniform1f(m_light1halfHeight, l1.m_HalfHeight);
  glUniform1f(m_light1distance, l1.m_Distance);
  glUniform1f(m_light1skyRadius, l1.m_SkyRadius);
  glUniform3fv(m_light1P, 1, glm::value_ptr(l1.m_P));
  glUniform3fv(m_light1target, 1, glm::value_ptr(l1.m_Target));
  glUniform3fv(m_light1N, 1, glm::value_ptr(l1.m_N));
  glUniform3fv(m_light1U, 1, glm::value_ptr(l1.m_U));
  glUniform3fv(m_light1V, 1, glm::value_ptr(l1.m_V));
  glUniform1f(m_light1area, l1.m_Area);
  glUniform1f(m_light1areaPdf, l1.m_AreaPdf);
  glUniform3fv(m_light1color, 1, glm::value_ptr(l1.m_Color * l1.m_ColorIntensity));
  glUniform3fv(m_light1colorTop, 1, glm::value_ptr(l1.m_ColorTop * l1.m_ColorTopIntensity));
  glUniform3fv(m_light1colorMiddle, 1, glm::value_ptr(l1.m_ColorMiddle * l1.m_ColorMiddleIntensity));
  glUniform3fv(m_light1colorBottom, 1, glm::value_ptr(l1.m_ColorBottom * l1.m_ColorBottomIntensity));
  glUniform1i(m_light1T, l1.m_T);

  // per channel
  int NC = scene->m_volume->sizeC();

  int activeChannel = 0;
  int luttex[4] = { 0, 0, 0, 0 };
  int colormaptex = imggpu.m_ActiveChannelColormaps;
  float intensitymax[4] = { 1, 1, 1, 1 };
  float intensitymin[4] = { 0, 0, 0, 0 };
  float lutmax[4] = { 1, 1, 1, 1 };
  float lutmin[4] = { 0, 0, 0, 0 };
  float labels[4] = { 0, 0, 0, 0 };
  float diffuse[3 * 4] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
  float specular[3 * 4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  float emissive[3 * 4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  float roughness[4] = { 0, 0, 0, 0 };
  float opacity[4] = { 0, 0, 0, 0 };
  // MUST MATCH SHADER pathTraceVolume.frag
  static constexpr int MAX_NO_TF_NODES = 16;
  float tfdata[4 * MAX_NO_TF_NODES * 2] = { 0 };
  uint32_t tfnodes[4] = { 0, 0, 0, 0 };

  for (int i = 0; i < NC; ++i) {
    if (scene->m_material.m_enabled[i] && activeChannel < MAX_GL_CHANNELS) {
      luttex[activeChannel] = imggpu.m_channels[i]->m_VolumeLutGLTexture;
      intensitymax[activeChannel] = scene->m_volume->channel(i)->m_histogram.getDataMax();
      intensitymin[activeChannel] = scene->m_volume->channel(i)->m_histogram.getDataMin();
      diffuse[activeChannel * 3 + 0] = scene->m_material.m_diffuse[i * 3 + 0];
      diffuse[activeChannel * 3 + 1] = scene->m_material.m_diffuse[i * 3 + 1];
      diffuse[activeChannel * 3 + 2] = scene->m_material.m_diffuse[i * 3 + 2];
      specular[activeChannel * 3 + 0] = scene->m_material.m_specular[i * 3 + 0];
      specular[activeChannel * 3 + 1] = scene->m_material.m_specular[i * 3 + 1];
      specular[activeChannel * 3 + 2] = scene->m_material.m_specular[i * 3 + 2];
      emissive[activeChannel * 3 + 0] = scene->m_material.m_emissive[i * 3 + 0];
      emissive[activeChannel * 3 + 1] = scene->m_material.m_emissive[i * 3 + 1];
      emissive[activeChannel * 3 + 2] = scene->m_material.m_emissive[i * 3 + 2];
      roughness[activeChannel] = scene->m_material.m_roughness[i];
      opacity[activeChannel] = scene->m_material.m_opacity[i];

      // get a min/max from the gradient data if possible
      uint16_t imin16 = 0;
      uint16_t imax16 = 0;
      bool hasMinMax =
        scene->m_material.m_gradientData[i].getMinMax(scene->m_volume->channel(i)->m_histogram, &imin16, &imax16);
      lutmin[activeChannel] = hasMinMax ? imin16 : intensitymin[activeChannel];
      lutmax[activeChannel] = hasMinMax ? imax16 : intensitymax[activeChannel];
      labels[activeChannel] = scene->m_material.m_labels[i];

      // copy control points in to tfdata
      const auto& tf = scene->m_material.m_gradientData[i].getControlPoints(scene->m_volume->channel(i)->m_histogram);
      int nTfPoints = std::min((int)tf.size(), MAX_NO_TF_NODES);
      tfnodes[activeChannel] = nTfPoints;
      for (int j = 0; j < nTfPoints; ++j) {
        tfdata[activeChannel * MAX_NO_TF_NODES * 2 + j * 2 + 0] = tf[j].first;
        tfdata[activeChannel * MAX_NO_TF_NODES * 2 + j * 2 + 1] = tf[j].second;
      }
      activeChannel++;
    }
  }
  glUniform1i(m_g_nChannels, activeChannel);
  check_gl("pre lut textures");

  glUniform2fv(m_tf, 4 * MAX_NO_TF_NODES, tfdata);
  glUniform4uiv(m_tf_nNodes, 1, tfnodes);

  glUniform1i(m_lutTexture0, 2);
  glActiveTexture(GL_TEXTURE0 + 2);
  glBindTexture(GL_TEXTURE_2D, luttex[0]);
  check_gl("lut 0");

  glUniform1i(m_lutTexture1, 3);
  glActiveTexture(GL_TEXTURE0 + 3);
  glBindTexture(GL_TEXTURE_2D, luttex[1]);
  check_gl("lut 1");

  glUniform1i(m_lutTexture2, 4);
  glActiveTexture(GL_TEXTURE0 + 4);
  glBindTexture(GL_TEXTURE_2D, luttex[2]);
  check_gl("lut 2");

  glUniform1i(m_lutTexture3, 5);
  glActiveTexture(GL_TEXTURE0 + 5);
  glBindTexture(GL_TEXTURE_2D, luttex[3]);
  check_gl("lut 3");

  glUniform1i(m_colormapTexture, 6);
  glActiveTexture(GL_TEXTURE0 + 6);
  glBindTexture(GL_TEXTURE_2D_ARRAY, colormaptex);
  check_gl("colormap array");

  glUniform4fv(m_intensityMax, 1, intensitymax);
  glUniform4fv(m_intensityMin, 1, intensitymin);
  glUniform4fv(m_lutMax, 1, lutmax);
  glUniform4fv(m_lutMin, 1, lutmin);
  glUniform4fv(m_labels, 1, labels);

  glUniform1fv(m_opacity, 4, opacity);
  glUniform3fv(m_emissive0, 1, emissive + 0);
  glUniform3fv(m_emissive1, 1, emissive + 3);
  glUniform3fv(m_emissive2, 1, emissive + 6);
  glUniform3fv(m_emissive3, 1, emissive + 9);
  glUniform3fv(m_diffuse0, 1, diffuse + 0);
  glUniform3fv(m_diffuse1, 1, diffuse + 3);
  glUniform3fv(m_diffuse2, 1, diffuse + 6);
  glUniform3fv(m_diffuse3, 1, diffuse + 9);
  glUniform3fv(m_specular0, 1, specular + 0);
  glUniform3fv(m_specular1, 1, specular + 3);
  glUniform3fv(m_specular2, 1, specular + 6);
  glUniform3fv(m_specular3, 1, specular + 9);
  glUniform1fv(m_roughness, 4, roughness);

  glUniform1i(m_uShowLights, 0);

  if (scene->m_clipPlane->m_enabled) {
    Plane p = Plane().transform(scene->m_clipPlane->m_transform.getMatrix());
    glUniform4fv(m_clipPlane, 1, glm::value_ptr(p.asVec4()));
  } else {
    glUniform4fv(m_clipPlane, 1, glm::value_ptr(glm::vec4(0, 0, 0, 0)));
  }

  check_gl("pathtrace shader uniform binding");
}

void
GLPTVolumeShader::setTransformUniforms(const CCamera& camera, const glm::mat4& modelMatrix)
{
}
