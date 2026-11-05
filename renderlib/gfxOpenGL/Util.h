#pragma once

#include "glad/glad.h"
#include "glm.h"

#include "Logging.h"

#include <string>

class Scene;

/**
 * Check OpenGL status.
 *
 * Call glGetError() and log the specified message with
 * additional details if a problem was encountered.
 *
 * @param message the message to log on error.
 */
extern void
check_gl(std::string const& message);
extern bool
check_glfb(std::string const& message);

class GLImageShader2DnoLut;
class RectImage2D
{

public:
  RectImage2D();
  ~RectImage2D();

  void draw(GLuint texture2d);

private:
  /// The vertex array.
  GLuint _quadVertexArray; // vao
  /// The image vertices.
  GLuint _quadVertices; // buffer
  /// The image texture coordinates.
  GLuint _quadTexcoords; // buffer
  /// The image elements.
  GLuint _quadIndices; // buffer
  size_t _num_image_elements;

  GLImageShader2DnoLut* _image_shader;
};

class GLFlatShader2D;
class BoundingBoxDrawable
{

public:
  BoundingBoxDrawable();
  ~BoundingBoxDrawable();

  void drawLines(const glm::mat4& transform, const glm::vec4& color);
  void drawTickMarks(const glm::mat4& transform, const glm::vec4& color);
  void drawFaces(const glm::mat4& transform, const glm::vec4& color);
  void updateTickMarks(const glm::vec3& scale, float maxPhysicalDim);

private:
  /// The vertex array.
  GLuint _vertexArray; // vao
  /// The image vertices.
  GLuint _vertices; // buffer

  /// A second vertex array for tick marks.
  GLuint _vertexArray2; // vao
  /// The tick mark vertices.
  GLuint _vertices2; // buffer
  size_t _num_tick_mark_floats;

  /// The image elements.
  GLuint _line_indices; // buffer
  GLuint _face_indices; // buffer
  size_t _num_line_elements;
  size_t _num_face_elements;

  GLFlatShader2D* _shader;
};

class GLTimer
{
public:
  GLTimer();
  virtual ~GLTimer();

  void StartTimer();
  float StopTimer();
  float ElapsedTime();

private:
  bool m_Started;
  GLuint m_EventStart;
  GLuint m_EventStop;

  void synchronize(GLuint eventid);
  void eventElapsedTime(float* result, GLuint startEvent, GLuint stopEvent);
};

// RAII; must have a current gl context at creation time.
class GLFramebufferObject
{
public:
  GLFramebufferObject(int width, int height, GLint colorInternalFormat);

  ~GLFramebufferObject();

  void bind();
  void release();
  int width() const;
  int height() const;

  // pixels must be preallocated with 32bits per pixel, ASSUMING RGBA internal format
  void toImage(void* pixels);

private:
  GLuint m_fbo;
  GLuint m_texture;
  GLuint m_depth_buffer;
  int m_width;
  int m_height;
};

class GLShader
{
public:
  GLShader(GLenum shaderType);
  ~GLShader();

  bool compileSourceCode(const char* sourceCode);
  bool isCompiled() const;
  std::string log() const;
  GLuint id() const { return m_shader; }

protected:
  bool m_isCompiled;
  GLuint m_shader;
  GLenum m_shaderType;
  std::string m_log;
};

class GLShaderProgram
{
public:
  GLShaderProgram();
  ~GLShaderProgram();

  void addShader(GLShader* shader);
  bool link();
  bool isLinked();
  int attributeLocation(const char* name);
  int uniformLocation(const char* name);

  void enableAttributeArray(int location);
  void disableAttributeArray(int location);
  void setAttributeArray(int location, const GLfloat* values, int tupleSize, int stride = 0);

  bool bind();
  void release();

  std::string log() { return m_log; }

protected:
  void utilMakeSimpleProgram(std::string const& vertexShaderSource,
                             std::string const& fragmentShaderSource,
                             GLShader** outVShader = nullptr,
                             GLShader** outFShader = nullptr);

private:
  // std::vector<GLShader> m_shaders;
  GLuint m_program;
  bool m_isLinked;
  std::string m_log;
};

class IGuiShader
{
public:
  virtual void setProjMatrix(const glm::mat4& proj) = 0;
};

void
clearFramebuffer(const Scene* scene);
