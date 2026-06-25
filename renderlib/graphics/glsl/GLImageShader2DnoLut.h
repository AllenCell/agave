#pragma once

#include "gfxOpenGL/Backend.h"

#include "gl/Util.h"

#include <glm.h>

/**
 * 2D image shader program (simple, up to three channels).
 */
class GLImageShader2DnoLut : public GLShaderProgram
{

public:
  /**
   * Constructor.
   *
   * @param parent the parent of this object.
   */
  explicit GLImageShader2DnoLut();

  /// Destructor.
  ~GLImageShader2DnoLut();

  /// Enable vertex coordinates.
  void enableCoords();

  /// Disable vertex coordinates.
  void disableCoords();

  /**
   * Set vertex coordinates from array.
   *
   * @param offset data offset if using a buffer object otherwise
   * the coordinate values.
   * @param tupleSize the tuple size of the data.
   * @param stride the stride of the data.
   */
  void setCoords(const GLfloat* offset = nullptr, int tupleSize = 2, int stride = 0);

  /**
   * Set vertex coordinates from buffer object.
   *
   * @param coords the coordinate values; null if using a buffer object.
   * @param offset the offset into the coords buffer.
   * @param tupleSize the tuple size of the data.
   * @param stride the stride of the data.
   */
  void setCoords(GLuint coords, const GLfloat* offset = nullptr, int tupleSize = 2, int stride = 0);

  /// Enable texture coordinates.
  void enableTexCoords();

  /// Disable texture coordinates.
  void disableTexCoords();

  /**
   * Set texture coordinates from array.
   *
   * @param offset data offset if using a buffer object otherwise
   * the coordinate values.
   * @param tupleSize the tuple size of the data.
   * @param stride the stride of the data.
   */
  void setTexCoords(const GLfloat* offset = nullptr, int tupleSize = 2, int stride = 0);

  /**
   * Set texture coordinates from buffer object.
   *
   * @param coords the coordinate values; null if using a buffer
   * object.
   * @param offset the offset into the coords buffer.
   * @param tupleSize the tuple size of the data.
   * @param stride the stride of the data.
   */
  void setTexCoords(GLuint coords, const GLfloat* offset = nullptr, int tupleSize = 2, int stride = 0);

  /**
   * Set the texture to render.
   *
   * @param texunit the texture unit to use.
   */
  void setTexture(int texunit);

  /**
   * Set model view projection matrix.
   *
   * @param mvp the model view projection matrix.
   */
  void setModelViewProjection(const glm::mat4& mvp);

private:
  /// The vertex shader.
  GLShader* m_vshader;
  /// The fragment shader.
  GLShader* m_fshader;

  /// Vertex coordinates attribute.
  int m_attr_coords;
  /// Texture coordinates attribute.
  int m_attr_texcoords;
  /// Model view projection uniform.
  int m_uniform_mvp;
  /// Texture uniform.
  int m_uniform_texture;
};
