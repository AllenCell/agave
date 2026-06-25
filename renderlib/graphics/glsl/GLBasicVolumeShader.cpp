#include "GLBasicVolumeShader.h"
#include "gfxOpenGL/Backend.h"

#include "Logging.h"
#include "shaders.h"

#include <gl/Util.h>
#include <glm.h>

#include <iostream>
#include <sstream>

GLBasicVolumeShader::GLBasicVolumeShader()
  : GLShaderProgram()
  , vshader()
  , fshader()
  , attr_coords()
{
  vshader = new GLShader(GL_VERTEX_SHADER);
  vshader->compileSourceCode(getShaderSource("basicVolume_vert").c_str());

  if (!vshader->isCompiled()) {
    LOG_ERROR << "GLBasicVolumeShader: Failed to compile vertex shader\n" << vshader->log();
  }

  fshader = new GLShader(GL_FRAGMENT_SHADER);
  fshader->compileSourceCode(getShaderSource("basicVolume_frag").c_str());

  if (!fshader->isCompiled()) {
    LOG_ERROR << "GLBasicVolumeShader: Failed to compile fragment shader\n" << fshader->log();
  }

  addShader(vshader);
  addShader(fshader);
  link();

  if (!isLinked()) {
    LOG_ERROR << "GLBasicVolumeShader: Failed to link shader program\n" << log();
  }

  attr_coords = attributeLocation("position");
  if (attr_coords == -1)
    LOG_ERROR << "GLBasicVolumeShader: Failed to bind coordinates";

  uModelViewMatrix = uniformLocation("modelViewMatrix");
  uProjectionMatrix = uniformLocation("projectionMatrix");

  uBreakSteps = uniformLocation("BREAK_STEPS");
  uAABBClipMin = uniformLocation("AABB_CLIP_MIN");
  uAABBClipMax = uniformLocation("AABB_CLIP_MAX");
  uFlipVolumeAxes = uniformLocation("flipVolumeAxes");
  uInverseModelViewMatrix = uniformLocation("inverseModelViewMatrix");
  // uCameraPosition = uniformLocation("cameraPosition");
  uGammaMin = uniformLocation("GAMMA_MIN");
  uGammaMax = uniformLocation("GAMMA_MAX");
  uGammaScale = uniformLocation("GAMMA_SCALE");
  uBrightness = uniformLocation("BRIGHTNESS");
  uDensity = uniformLocation("DENSITY");
  uMaskAlpha = uniformLocation("maskAlpha");

  uTextureAtlas = uniformLocation("textureAtlas");
  uTextureAtlasMask = uniformLocation("textureAtlasMask");

  uDataRangeMin = uniformLocation("dataRangeMin");
  uDataRangeMax = uniformLocation("dataRangeMax");

  uIsPerspective = uniformLocation("isPerspective");
  uOrthoScale = uniformLocation("orthoScale");
  uResolution = uniformLocation("iResolution");

  uClipPlane = uniformLocation("g_clipPlane");
}

GLBasicVolumeShader::~GLBasicVolumeShader() {}

void
GLBasicVolumeShader::enableCoords()
{
  enableAttributeArray(attr_coords);
}

void
GLBasicVolumeShader::disableCoords()
{
  disableAttributeArray(attr_coords);
}

void
GLBasicVolumeShader::setCoords(const GLfloat* offset, int tupleSize, int stride)
{
  setAttributeArray(attr_coords, offset, tupleSize, stride);
}

void
GLBasicVolumeShader::setCoords(GLuint coords, const GLfloat* offset, int tupleSize, int stride)
{
  glBindBuffer(GL_ARRAY_BUFFER, coords);
  setCoords(offset, tupleSize, stride);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
GLBasicVolumeShader::setTexture(int texunit)
{
  glUniform1i(uTextureAtlas, texunit);
  check_gl("Set image texture");
}

void
GLBasicVolumeShader::setCorrection(const glm::vec3& corr)
{
  //    glUniform3fv(uniform_corr, 1, glm::value_ptr(corr));
  check_gl("Set correction multiplier");
}

void
GLBasicVolumeShader::setLUT(int texunit)
{
  //   glUniform1i(uniform_lut, texunit);
  check_gl("Set LUT texture");
}

void
GLBasicVolumeShader::setShadingUniforms()
{
  glUniform1f(uDataRangeMin, dataRangeMin);
  glUniform1f(uDataRangeMax, dataRangeMax);
  glUniform1f(uGammaMin, GAMMA_MIN);
  glUniform1f(uGammaMax, GAMMA_MAX);
  glUniform1f(uGammaScale, GAMMA_SCALE);
  glUniform1f(uBrightness, BRIGHTNESS);
  glUniform1f(uDensity, DENSITY);
  glUniform1f(uMaskAlpha, maskAlpha);
  glUniform1f(uIsPerspective, isPerspective);
  glUniform1f(uOrthoScale, orthoScale);
  glUniform1i(uBreakSteps, BREAK_STEPS);
  // axis aligned clip planes
  glUniform3fv(uAABBClipMin, 1, glm::value_ptr(AABB_CLIP_MIN));
  glUniform3fv(uAABBClipMax, 1, glm::value_ptr(AABB_CLIP_MAX));
  glUniform3fv(uFlipVolumeAxes, 1, glm::value_ptr(flipVolumeAxes));
  glUniform2fv(uResolution, 1, glm::value_ptr(resolution));
  glUniform4fv(uClipPlane, 1, glm::value_ptr(clipPlane));
}

void
GLBasicVolumeShader::setTransformUniforms(const CCamera& camera, const glm::mat4& modelMatrix)
{
  glm::mat4 cv(1.0);
  camera.getViewMatrix(cv);
  glm::mat4 cp(1.0);
  camera.getProjMatrix(cp);

  glm::mat4 mv = cv * modelMatrix;

  glUniformMatrix4fv(uProjectionMatrix, 1, GL_FALSE, glm::value_ptr(cp));
  glUniformMatrix4fv(uModelViewMatrix, 1, GL_FALSE, glm::value_ptr(mv));
  glUniformMatrix4fv(uInverseModelViewMatrix, 1, GL_FALSE, glm::value_ptr(glm::inverse(mv)));
}
