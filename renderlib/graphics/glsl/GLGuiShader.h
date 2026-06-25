#pragma once

#include "gfxOpenGL/Util.h"

class GLGuiShader
  : public GLShaderProgram
  , public IGuiShader
{
public:
  GLGuiShader();

  ~GLGuiShader() {}

  void configure(bool display, GLuint textureId);
  void cleanup();
  void setProjMatrix(const glm::mat4& proj) override;

  int m_loc_proj;
  int m_loc_vpos;
  int m_loc_vuv;
  int m_loc_vcol;
  int m_loc_vcode;
};
