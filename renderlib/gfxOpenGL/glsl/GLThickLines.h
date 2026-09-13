#pragma once

#include "gfxOpenGL/Util.h"

class GLThickLinesShader
  : public GLShaderProgram
  , public IGuiShader
{
public:
  GLThickLinesShader();

  ~GLThickLinesShader();

  void configure(bool display, GLuint textureId);
  void cleanup();

  void setProjMatrix(const glm::mat4& proj) override;

  int m_loc_proj;
  int m_loc_vpos;
  int m_loc_vuv;
  int m_loc_vcol;
  int m_loc_vcode;
  int m_loc_thickness;
  int m_loc_resolution;
  int m_loc_stripVerts;
  int m_loc_stripVertexOffset;
};
