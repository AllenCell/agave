#include "gfxOpenGL/Backend.h"

#include "GLToneMapShader.h"

#include "Logging.h"
#include "shaders.h"

#include <gl/Util.h>
#include <glm.h>

#include <iostream>
#include <sstream>

GLToneMapShader::GLToneMapShader()
  : GLShaderProgram()
  , m_vshader()
  , m_fshader()
{
  m_vshader = new GLShader(GL_VERTEX_SHADER);
  m_vshader->compileSourceCode(getShaderSource("toneMap_vert").c_str());

  if (!m_vshader->isCompiled()) {
    LOG_ERROR << "GLToneMapShader: Failed to compile vertex shader\n" << m_vshader->log();
  }

  m_fshader = new GLShader(GL_FRAGMENT_SHADER);
  m_fshader->compileSourceCode(getShaderSource("toneMap_frag").c_str());

  if (!m_fshader->isCompiled()) {
    LOG_ERROR << "GLToneMapShader: Failed to compile fragment shader\n" << m_fshader->log();
  }

  addShader(m_vshader);
  addShader(m_fshader);
  link();

  if (!isLinked()) {
    LOG_ERROR << "GLToneMapShader: Failed to link shader program\n" << log();
  }

  m_texture = uniformLocation("tTexture0");
  m_InvExposure = uniformLocation("gInvExposure");
}

GLToneMapShader::~GLToneMapShader() {}

void
GLToneMapShader::setShadingUniforms(float invExposure)
{
  glUniform1i(m_texture, 0);
  glUniform1f(m_InvExposure, invExposure);
}
