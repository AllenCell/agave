#include "GLGuiShader.h"

#include "shaders.h"

GLGuiShader::GLGuiShader()
  : GLShaderProgram()
{
  utilMakeSimpleProgram(getShaderSource("gui_vert").c_str(), getShaderSource("gui_frag").c_str());

  m_loc_proj = uniformLocation("projection");
  m_loc_vpos = attributeLocation("vPos");
  m_loc_vuv = attributeLocation("vUV");
  m_loc_vcol = attributeLocation("vCol");
  m_loc_vcode = attributeLocation("vCode");
}

void
GLGuiShader::configure(bool display, GLuint textureId)
{
  bind();
  check_gl("bind gesture draw shader");

  glEnableVertexAttribArray(m_loc_vpos);
  check_gl("enable vertex attrib array 0");
  glEnableVertexAttribArray(m_loc_vuv);
  check_gl("enable vertex attrib array 1");
  glEnableVertexAttribArray(m_loc_vcol);
  check_gl("enable vertex attrib array 2");
  glEnableVertexAttribArray(m_loc_vcode);
  check_gl("enable vertex attrib array 3");

  glUniform1i(uniformLocation("picking"), display ? 0 : 1);
  check_gl("set picking uniform");
  if (display)
    glUniform1i(uniformLocation("Texture"), 0);
  else
    glUniform1i(uniformLocation("Texture"), 1);
  check_gl("set texture uniform");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureId);
  check_gl("bind texture");
}

void
GLGuiShader::cleanup()
{
  release();
  glDisableVertexAttribArray(m_loc_vpos);
  glDisableVertexAttribArray(m_loc_vuv);
  glDisableVertexAttribArray(m_loc_vcol);
  glDisableVertexAttribArray(m_loc_vcode);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void
GLGuiShader::setProjMatrix(const glm::mat4& proj)
{
  glUniformMatrix4fv(m_loc_proj, 1, GL_FALSE, glm::value_ptr(proj));
}
