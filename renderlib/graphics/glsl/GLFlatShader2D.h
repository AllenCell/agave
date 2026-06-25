#pragma once

#include "gfxOpenGL/Backend.h"

#include "gl/Util.h"

#include <glm.h>

/**
 * 2D flat (solid fill) shader program.
 */
class GLFlatShader2D : public GLShaderProgram
{

public:
  /**
   * Constructor.
   *
   * @param parent the parent of this object.
   */
  explicit GLFlatShader2D();

  /// Destructor.
  ~GLFlatShader2D();

  /// @copydoc GLImageShader2D::enableCoords()
  void enableCoords();

  /// @copydoc GLImageShader2D::enableCoords()
  void disableCoords();

  /// @copydoc GLImageShader2D::setCoords(const GLfloat*, int, int)
  void setCoords(const GLfloat* offset, int tupleSize, int stride = 0);

  /// @copydoc GLImageShader2D::setCoords(QOpenGLBuffer&, const GLfloat*, int, int)
  void setCoords(GLuint coords, const GLfloat* offset, int tupleSize, int stride = 0);

  /**
   * Set fill colour.
   *
   * @param colour the RGBA fill colour.
   */
  void setColour(const glm::vec4& colour);

  /// @copydoc GLImageShader2D::setModelViewProjection(const glm::mat4& mvp)
  void setModelViewProjection(const glm::mat4& mvp);

private:
  /// @copydoc GLImageShader2D::vshader
  GLShader* m_vshader;
  /// @copydoc GLImageShader2D::fshader
  GLShader* m_fshader;

  /// @copydoc GLImageShader2D::attr_coords
  int m_attr_coords;
  /// Fill colour uniform.
  int m_uniform_colour;
  /// @copydoc GLImageShader2D::uniform_mvp
  int m_uniform_mvp;
};
