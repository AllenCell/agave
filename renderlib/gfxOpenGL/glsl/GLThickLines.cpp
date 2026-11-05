#include "GLThickLines.h"

#include "glm.h"
#include "shaders.h"

#include <vector>
#include <string>

// * create an array with the points of the line strip.
// * first and last point define the tangents of the start and end of the line strip,
// so you need to add one pt at start and end
// * if drawing a line loop, then the last point has to be added to the array head,
// and the first point added to the tail
// * store the array of pts in a buffer that the shader can access by indexing (ideally SSBO?)
// * shader doesn't need any vertex coordinates or attributes. It just needs to know the index of the line segment.
// * to get the line segment index we just use the index of the vertex currently being processed (gl_VertexID)
// * to draw a line strip with N points (N-1 segments), we need 6*(N-1) vertices
// * each segment is 2 triangles. glDrawArrays(GL_TRIANGLES, 0, 6*(N-1)) will draw the line strip
// line index = gl_VertexID / 6
// tri index = gl_VertexID % 6
// * Since we are drawing N-1 line segments, but the number of elements in the array is N+2,
// the elements from vertex[line_t] to vertex[line_t+3] can be accessed for each vertex which
// is processed in the vertex shader.
// * vertex[line_t+1] and vertex[line_t+2] are the start and end coordinate of the line segment.
// * vertex[line_t] and vertex[line_t+3] are required to compute the miter.
// thickness is provided in pixels, and so we need to convert it to clip space and use window resolution

GLThickLinesShader::GLThickLinesShader()
  : GLShaderProgram()
{
  utilMakeSimpleProgram(getShaderSource("thickLines_vert").c_str(), getShaderSource("thickLines_frag").c_str());

  m_loc_proj = uniformLocation("projection");
  // m_loc_vpos = attributeLocation("vPos");
  // m_loc_vuv = attributeLocation("vUV");
  // m_loc_vcol = attributeLocation("vCol");
  // m_loc_vcode = attributeLocation("vCode");
  m_loc_thickness = uniformLocation("thickness");
  m_loc_resolution = uniformLocation("resolution");
  m_loc_stripVerts = uniformLocation("stripVerts");
  m_loc_stripVertexOffset = uniformLocation("stripVertexOffset");
}
GLThickLinesShader::~GLThickLinesShader() {}

void
GLThickLinesShader::configure(bool display, GLuint textureId)
{
  bind();
  check_gl("bind gesture draw thicklines shader");

  glUniform1i(uniformLocation("picking"), display ? 0 : 1);
  check_gl("set picking uniform");
  if (display)
    glUniform1i(uniformLocation("Texture"), 0);
  else
    glUniform1i(uniformLocation("Texture"), 1);
  check_gl("set texture uniform");
  // glActiveTexture(GL_TEXTURE0);
  // glBindTexture(GL_TEXTURE_2D, textureId);
  check_gl("bind texture");
}

void
GLThickLinesShader::cleanup()
{
  release();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_BUFFER, 0);

  // glBindVertexArray(0);
}

void
GLThickLinesShader::setProjMatrix(const glm::mat4& proj)
{
  glUniformMatrix4fv(m_loc_proj, 1, GL_FALSE, glm::value_ptr(proj));
}
