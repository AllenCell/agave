#include "Util.h"

#include "Logging.h"
#include "BoundingBox.h"
#include "MathUtil.h"
#include "AppScene.h"
#include "glsl/GLFlatShader2D.h"
#include "glsl/GLImageShader2DnoLut.h"

#include "glm.h"

#include <array>
#include <iostream>
#include <vector>

static bool GL_ERROR_CHECKS_ENABLED = true;

bool
check_glfb(std::string const& message)
{
  if (!GL_ERROR_CHECKS_ENABLED) {
    return true;
  }
  check_gl(message);
  GLint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::string statusstr = "Unknown " + std::to_string(status);
    switch (status) {
      case GL_FRAMEBUFFER_UNDEFINED:
        statusstr = "GL_FRAMEBUFFER_UNDEFINED";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
        break;
      case GL_FRAMEBUFFER_UNSUPPORTED:
        statusstr = "GL_FRAMEBUFFER_UNSUPPORTED";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
        statusstr = "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
        break;
      default:
        break;
    }
    LOG_DEBUG << "Framebuffer not complete! Error code: " << statusstr;
    return false;
  } else {
    return true;
  }
}

void
check_gl(std::string const& message)
{
  if (!GL_ERROR_CHECKS_ENABLED) {
    return;
  }

  GLenum err = GL_NO_ERROR;
  while ((err = glGetError()) != GL_NO_ERROR) {
    std::string msg = "GL error (" + message + ") :";
    switch (err) {
      case GL_INVALID_ENUM:
        msg += "Invalid enum";
        break;
      case GL_INVALID_VALUE:
        msg += "Invalid value";
        break;
      case GL_INVALID_OPERATION:
        msg += "Invalid operation";
        break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        msg += "Invalid framebuffer operation";
        break;
      case GL_OUT_OF_MEMORY:
        msg += "Out of memory";
        break;
      case GL_STACK_UNDERFLOW:
        msg += "Stack underflow";
        break;
      case GL_STACK_OVERFLOW:
        msg += "Stack overflow";
        break;
      default:
        msg += "Unknown (" + std::to_string(err) + ')';
        break;
    }
    LOG_DEBUG << msg;
  }
}

RectImage2D::RectImage2D()
{
  // setup geometry
  glm::vec2 xlim(-1.0, 1.0);
  glm::vec2 ylim(-1.0, 1.0);
  const std::array<GLfloat, 8> square_vertices{
    xlim[0], ylim[0], xlim[1], ylim[0], xlim[1], ylim[1], xlim[0], ylim[1]
  };

  glGenVertexArrays(1, &_quadVertexArray);
  glBindVertexArray(_quadVertexArray);
  check_gl("create and bind verts");

  glGenBuffers(1, &_quadVertices);
  glBindBuffer(GL_ARRAY_BUFFER, _quadVertices);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * square_vertices.size(), square_vertices.data(), GL_STATIC_DRAW);
  check_gl("init vtx coord data");

  glm::vec2 texxlim(0.0, 1.0);
  glm::vec2 texylim(0.0, 1.0);
  std::array<GLfloat, 8> square_texcoords{ texxlim[0], texylim[0], texxlim[1], texylim[0],
                                           texxlim[1], texylim[1], texxlim[0], texylim[1] };

  glGenBuffers(1, &_quadTexcoords);
  glBindBuffer(GL_ARRAY_BUFFER, _quadTexcoords);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * square_texcoords.size(), square_texcoords.data(), GL_STATIC_DRAW);
  check_gl("init texcoord data");

  std::array<GLushort, 6> square_elements{ // front
                                           0, 1, 2, 2, 3, 0
  };

  glGenBuffers(1, &_quadIndices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _quadIndices);
  glBufferData(
    GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * square_elements.size(), square_elements.data(), GL_STATIC_DRAW);
  _num_image_elements = square_elements.size();
  check_gl("init element data");

  glBindVertexArray(0);
  check_gl("unbind vtx array");

  _image_shader = new GLImageShader2DnoLut();
}

RectImage2D::~RectImage2D()
{
  delete _image_shader;

  glDeleteVertexArrays(1, &_quadVertexArray);
  _quadVertexArray = 0;
  glDeleteBuffers(1, &_quadVertices);
  _quadVertices = 0;
  glDeleteBuffers(1, &_quadTexcoords);
  _quadTexcoords = 0;
  glDeleteBuffers(1, &_quadIndices);
  _quadIndices = 0;
}

void
RectImage2D::draw(GLuint texture2d)
{
  _image_shader->bind();
  check_gl("Bind shader for RectImage2D");

  _image_shader->setModelViewProjection(glm::mat4(1.0));

  glActiveTexture(GL_TEXTURE0);
  check_gl("Activate texture");
  glBindTexture(GL_TEXTURE_2D, texture2d);
  check_gl("Bind texture");
  _image_shader->setTexture(0);

  glBindVertexArray(_quadVertexArray);
  check_gl("bind vtx buf");

  _image_shader->enableCoords();
  _image_shader->setCoords(_quadVertices, nullptr, 2);

  _image_shader->enableTexCoords();
  _image_shader->setTexCoords(_quadTexcoords, nullptr, 2);

  // Push each element to the vertex shader
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _quadIndices);
  check_gl("bind element buf");
  glDrawElements(GL_TRIANGLES, (GLsizei)_num_image_elements, GL_UNSIGNED_SHORT, nullptr);
  check_gl("RectImage2D draw elements");

  _image_shader->disableCoords();
  _image_shader->disableTexCoords();
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);

  _image_shader->release();
}

BoundingBoxDrawable::BoundingBoxDrawable()
{
  // setup geometry
  const std::array<GLfloat, 8 * 3> square_vertices{ // front
                                                    -1.0,
                                                    -1.0,
                                                    1.0,
                                                    1.0,
                                                    -1.0,
                                                    1.0,
                                                    1.0,
                                                    1.0,
                                                    1.0,
                                                    -1.0,
                                                    1.0,
                                                    1.0,
                                                    // back
                                                    -1.0,
                                                    -1.0,
                                                    -1.0,
                                                    1.0,
                                                    -1.0,
                                                    -1.0,
                                                    1.0,
                                                    1.0,
                                                    -1.0,
                                                    -1.0,
                                                    1.0,
                                                    -1.0
  };

  glGenVertexArrays(1, &_vertexArray);
  glBindVertexArray(_vertexArray);
  check_gl("create and bind verts");

  glGenBuffers(1, &_vertices);
  glBindBuffer(GL_ARRAY_BUFFER, _vertices);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * square_vertices.size(), square_vertices.data(), GL_STATIC_DRAW);
  check_gl("init vtx coord data");

  // 12 line segments to make the box
  std::array<GLushort, 12 * 2> box_elements{
    0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7,
  };
  std::array<GLushort, 36> face_elements{ // front
                                          0,
                                          1,
                                          2,
                                          2,
                                          3,
                                          0,
                                          // right
                                          1,
                                          5,
                                          6,
                                          6,
                                          2,
                                          1,
                                          // back
                                          7,
                                          6,
                                          5,
                                          5,
                                          4,
                                          7,
                                          // left
                                          4,
                                          0,
                                          3,
                                          3,
                                          7,
                                          4,
                                          // bottom
                                          4,
                                          5,
                                          1,
                                          1,
                                          0,
                                          4,
                                          // top
                                          3,
                                          2,
                                          6,
                                          6,
                                          7,
                                          3
  };

  glGenBuffers(1, &_face_indices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _face_indices);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * face_elements.size(), face_elements.data(), GL_STATIC_DRAW);
  _num_face_elements = face_elements.size();
  glGenBuffers(1, &_line_indices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _line_indices);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * box_elements.size(), box_elements.data(), GL_STATIC_DRAW);
  _num_line_elements = box_elements.size();
  check_gl("init element data");

  glBindVertexArray(0);

  // set up empty placeholder for now.
  _num_tick_mark_floats = 0;
  glGenVertexArrays(1, &_vertexArray2);
  glBindVertexArray(_vertexArray2);
  check_gl("create and bind verts for tickmarks");

  glGenBuffers(1, &_vertices2);
  glBindBuffer(GL_ARRAY_BUFFER, _vertices2);
  // will be filled in from createTickMarks

  glBindVertexArray(0);
  check_gl("unbind vtx array");

  _shader = new GLFlatShader2D();
}

BoundingBoxDrawable::~BoundingBoxDrawable()
{
  delete _shader;

  glDeleteVertexArrays(1, &_vertexArray);
  _vertexArray = 0;
  glDeleteBuffers(1, &_vertices);
  _vertices = 0;
  glDeleteBuffers(1, &_line_indices);
  _line_indices = 0;
  glDeleteBuffers(1, &_face_indices);
  _face_indices = 0;

  glDeleteVertexArrays(1, &_vertexArray2);
  _vertexArray2 = 0;
  glDeleteBuffers(1, &_vertices2);
  _vertices2 = 0;
}

void
BoundingBoxDrawable::drawLines(const glm::mat4& transform, const glm::vec4& color)
{
  _shader->bind();
  check_gl("Bind shader for bounding box drawable lines");

  _shader->setModelViewProjection(transform);
  _shader->setColour(color);

  glBindVertexArray(_vertexArray);
  check_gl("bind vtx buf");

  _shader->enableCoords();
  _shader->setCoords(_vertices, nullptr, 3);

  // Push each element to the vertex shader
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _line_indices);
  check_gl("bind element buf");
  glDrawElements(GL_LINES, (GLsizei)_num_line_elements, GL_UNSIGNED_SHORT, nullptr);
  check_gl("bounding box draw elements");

  _shader->disableCoords();
  glBindVertexArray(0);

  _shader->release();
}

// Generate tick marks for a bounding box whose vertices span -1,-1,-1 to 1,1,1.
// Know that the box will be scaled proportional to the normPhysicalSize
// physicalscale is max of physical dims x,y,z
// normPhysicalSize is the normalized physical size of the volume (physical size / physicalscale)
std::vector<float>
createTickMarks(const float physicalScale, const glm::vec3 normPhysicalSize)
{
  std::vector<float> vertices;
  // Length of tick mark lines in world units
  static constexpr float TICK_LENGTH = 0.025f;
  // this will always be some integer power of 10?
  const float tickMarkPhysicalSpacing = computePhysicalScaleBarSize(physicalScale);
  const float maxNumTickMarks = physicalScale / tickMarkPhysicalSpacing;

  // generate tick mark vertices for each edge one edge at a time.
  CBoundingBox bbox(glm::vec3(-1.0f, -1.0f, -1.0f) * normPhysicalSize, glm::vec3(1.0f, 1.0f, 1.0f) * normPhysicalSize);
  std::array<glm::vec3, 8> corners;
  bbox.GetCorners(corners);

  std::vector<glm::vec3> tickVertices;
  // loop over edges:
  for (size_t i = 0; i < 12; ++i) {
    bbox.GetEdgeTickMarkVertices(corners[CBoundingBox::EDGES_ARRAY[i].a],
                                 corners[CBoundingBox::EDGES_ARRAY[i].b],
                                 maxNumTickMarks,
                                 TICK_LENGTH,
                                 tickVertices);
  }

  // un-scale the tick mark coordinates based on the scaling that will be our transform later.
  for (const auto& v : tickVertices) {
    vertices.push_back(v.x / normPhysicalSize.x);
    vertices.push_back(v.y / normPhysicalSize.y);
    vertices.push_back(v.z / normPhysicalSize.z);
  }

  return vertices;
}

void
BoundingBoxDrawable::updateTickMarks(const glm::vec3& scale, float maxPhysicalDim)
{
  // TODO do some kind of check to avoid repeating this work every time?
  std::vector<float> tickVerts = createTickMarks(maxPhysicalDim, scale);

  glBindVertexArray(_vertexArray2);
  if (tickVerts.size() != _num_tick_mark_floats) {
    // if count has changed then let's recreate the buffer?
    glDeleteBuffers(1, &_vertices2);
    glGenBuffers(1, &_vertices2);
  }
  glBindBuffer(GL_ARRAY_BUFFER, _vertices2);
  // upload data to gpu
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * tickVerts.size(), tickVerts.data(), GL_STATIC_DRAW);

  _num_tick_mark_floats = tickVerts.size();
  check_gl("init tickmark buffer data");
}

void
BoundingBoxDrawable::drawTickMarks(const glm::mat4& transform, const glm::vec4& color)
{
  _shader->bind();
  check_gl("Bind shader for bounding box drawable tick mark lines");

  _shader->setModelViewProjection(transform);
  _shader->setColour(color);

  glBindVertexArray(_vertexArray2);
  check_gl("bind vtx buf");

  _shader->enableCoords();
  // 3 floats per vertex
  _shader->setCoords(_vertices2, nullptr, 3);

  // Push each element to the vertex shader
  glDrawArrays(GL_LINES, 0, (GLsizei)_num_tick_mark_floats / 3);
  check_gl("bounding box draw elements");

  _shader->disableCoords();
  glBindVertexArray(0);

  _shader->release();
}

void
BoundingBoxDrawable::drawFaces(const glm::mat4& transform, const glm::vec4& color)
{
  _shader->bind();
  check_gl("Bind shader for bounding box drawable faces");

  _shader->setModelViewProjection(transform);
  _shader->setColour(color);

  glBindVertexArray(_vertexArray);
  check_gl("bind vtx buf");

  _shader->enableCoords();
  _shader->setCoords(_vertices, nullptr, 3);

  // Push each element to the vertex shader
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _face_indices);
  check_gl("bind element buf");
  glDrawElements(GL_TRIANGLES, (GLsizei)_num_face_elements, GL_UNSIGNED_SHORT, nullptr);
  check_gl("bounding box draw elements");

  _shader->disableCoords();
  glBindVertexArray(0);

  _shader->release();
}

GLTimer::GLTimer()
{
  StartTimer();
}

GLTimer::~GLTimer()
{
  glDeleteQueries(1, &m_EventStart);
  glDeleteQueries(1, &m_EventStop);
}

void
GLTimer::StartTimer()
{
  glGenQueries(1, &m_EventStart);
  glGenQueries(1, &m_EventStop);
  glQueryCounter(m_EventStart, GL_TIMESTAMP);

  m_Started = true;
}

float
GLTimer::StopTimer()
{
  if (!m_Started)
    return 0.0f;

  glQueryCounter(m_EventStop, GL_TIMESTAMP);
  synchronize(m_EventStop);

  float TimeDelta = 0.0f;

  eventElapsedTime(&TimeDelta, m_EventStart, m_EventStop);
  glDeleteQueries(1, &m_EventStart);
  glDeleteQueries(1, &m_EventStop);

  m_Started = false;

  return TimeDelta;
}

float
GLTimer::ElapsedTime()
{
  if (!m_Started)
    return 0.0f;

  glQueryCounter(m_EventStop, GL_TIMESTAMP);
  synchronize(m_EventStop);

  float TimeDelta = 0.0f;

  eventElapsedTime(&TimeDelta, m_EventStart, m_EventStop);

  m_Started = false;

  return TimeDelta;
}

void
GLTimer::synchronize(GLuint eventid)
{
  // wait until the results are available
  GLint stopTimerAvailable = 0;
  while (!stopTimerAvailable) {
    glGetQueryObjectiv(eventid, GL_QUERY_RESULT_AVAILABLE, &stopTimerAvailable);
  }
}

void
GLTimer::eventElapsedTime(float* result, GLuint startEvent, GLuint stopEvent)
{
  GLuint64 startTime, stopTime;
  // get query results
  glGetQueryObjectui64v(startEvent, GL_QUERY_RESULT, &startTime);
  glGetQueryObjectui64v(stopEvent, GL_QUERY_RESULT, &stopTime);

  *result = (float)((double)(stopTime - startTime) / 1000000.0);
}

GLFramebufferObject::GLFramebufferObject(int width, int height, GLint colorInternalFormat)
{
  m_fbo = 0;
  m_texture = 0;
  m_depth_buffer = 0;
  m_width = 0;
  m_height = 0;

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  // init texture
  GLuint target = GL_TEXTURE_2D;
  GLuint texture = 0;

  glGenTextures(1, &texture);
  glBindTexture(target, texture);

  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(target, 0, colorInternalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, target, texture, 0);
  // unbind as current gl texture
  glBindTexture(target, 0);

  // init depth
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
  // depth and stencil buffer needs another extension
  GLuint depth_buffer;
  glGenRenderbuffers(1, &depth_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
  if (!glIsRenderbuffer(depth_buffer)) {
    LOG_ERROR << "framebuffer depth buffer is not renderbuffer";
  }
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

  GLuint stencil_buffer = depth_buffer;
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil_buffer);

  bool valid = check_glfb("GLFramebufferObject creation");
  if (!valid) {
    glDeleteTextures(1, &texture);
    glDeleteRenderbuffers(1, &depth_buffer);
    glDeleteFramebuffers(1, &fbo);
  } else {
    m_fbo = fbo;
    m_texture = texture;
    m_depth_buffer = depth_buffer;
    m_width = width;
    m_height = height;
  }
}

GLFramebufferObject::~GLFramebufferObject()
{
  glDeleteTextures(1, &m_texture);
  glDeleteRenderbuffers(1, &m_depth_buffer);
  glDeleteFramebuffers(1, &m_fbo);
}

void
GLFramebufferObject::bind()
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}
void
GLFramebufferObject::release()
{
  // TODO what about headless offscreen?  there is no default surface?
  // glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
int
GLFramebufferObject::width() const
{
  return m_width;
}
int
GLFramebufferObject::height() const
{
  return m_height;
}

// pixels must be preallocated with 32bits per pixel, ASSUMING RGBA internal format
void
GLFramebufferObject::toImage(void* pixels)
{
  GLuint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&prevFbo);

  if (prevFbo != m_fbo) {
    bind();
  }

  glReadBuffer(GL_COLOR_ATTACHMENT0 + 0);

  const int w = width();
  const int h = height();

  // ASSUMING RGBA internal format
  glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);

  glReadBuffer(GL_COLOR_ATTACHMENT0);
  if (prevFbo != m_fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
  }
}

GLShader::GLShader(GLenum shaderType)
{
  m_isCompiled = false;
  m_shader = glCreateShader(shaderType);
  m_shaderType = shaderType;
}

GLShader::~GLShader()
{
  glDeleteShader(m_shader);
}

bool
GLShader::compileSourceCode(const char* sourceCode)
{
  glShaderSource(m_shader, 1, &sourceCode, nullptr);

  glCompileShader(m_shader);

  GLint value = 0;

  // Get compilation status
  glGetShaderiv(m_shader, GL_COMPILE_STATUS, &value);
  m_isCompiled = (value != 0);

  if (!m_isCompiled) {
    const char* types[] = { "Fragment", "Vertex", "Geometry", "Tessellation Control", "Tessellation Evaluation",
                            "Compute",  "" };

    const char* type = types[6];
    switch (m_shaderType) {
      case GL_FRAGMENT_SHADER:
        type = types[0];
        break;
      case GL_VERTEX_SHADER:
        type = types[1];
        break;
      case GL_GEOMETRY_SHADER:
        type = types[2];
        break;
      case GL_TESS_CONTROL_SHADER:
        type = types[3];
        break;
      case GL_TESS_EVALUATION_SHADER:
        type = types[4];
        break;
      case GL_COMPUTE_SHADER:
        type = types[5];
        break;
      default:
        break;
    }

    // Get info and source code lengths
    GLint infoLogLength = 0;
    GLint sourceCodeLength = 0;
    char* logBuffer = nullptr;
    char* sourceCodeBuffer = nullptr;

    // Get the compilation info log
    glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &infoLogLength);

    if (infoLogLength > 1) {
      GLint temp;
      logBuffer = new char[infoLogLength];
      glGetShaderInfoLog(m_shader, infoLogLength, &temp, logBuffer);
    }

    // Get the source code
    glGetShaderiv(m_shader, GL_SHADER_SOURCE_LENGTH, &sourceCodeLength);

    if (sourceCodeLength > 1) {
      GLint temp;
      sourceCodeBuffer = new char[sourceCodeLength];
      glGetShaderSource(m_shader, sourceCodeLength, &temp, sourceCodeBuffer);
    }

    if (logBuffer)
      m_log = logBuffer;
    else
      m_log = "failed";

    LOG_WARNING << "GLShader::compile(" << std::string(type) << "): " << m_log;

    // Dump the source code if we got it
    if (sourceCodeBuffer) {
      LOG_WARNING << "*** Problematic " << std::string(type) << " shader source code ***\n"
                  << std::string(sourceCodeBuffer);
    }

    // Cleanup
    delete[] logBuffer;
    delete[] sourceCodeBuffer;
  }

  return m_isCompiled;
}

bool
GLShader::isCompiled() const
{
  return m_isCompiled;
}

std::string
GLShader::log() const
{
  return m_log;
}

GLShaderProgram::GLShaderProgram()
{
  m_isLinked = false;
  m_program = glCreateProgram();
}

GLShaderProgram::~GLShaderProgram()
{
  if (glIsProgram(m_program)) {
    glDeleteProgram(m_program);
  }
}

void
GLShaderProgram::addShader(GLShader* shader)
{
  glAttachShader(m_program, shader->id());
  m_isLinked = false;
}

bool
GLShaderProgram::link()
{
  GLint value;

  // Check to see if the program is already linked and
  // bail out if so.
  value = 0;
  glGetProgramiv(m_program, GL_LINK_STATUS, &value);
  m_isLinked = (value != 0);
  if (m_isLinked) {
    return true;
  }

  glLinkProgram(m_program);
  value = 0;
  glGetProgramiv(m_program, GL_LINK_STATUS, &value);
  m_isLinked = (value != 0);
  value = 0;
  glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &value);
  m_log = "";
  if (value > 1) {
    char* logbuf = new char[value];
    GLint len;
    glGetProgramInfoLog(m_program, value, &len, logbuf);
    m_log = logbuf;
    if (!m_isLinked) {
      LOG_WARNING << "GLShaderProgram::link: " << m_log;
    }
    delete[] logbuf;
  }
  return m_isLinked;
}

bool
GLShaderProgram::isLinked()
{
  return m_isLinked;
}

int
GLShaderProgram::attributeLocation(const char* name)
{
  if (m_isLinked && m_program) {
    return glGetAttribLocation(m_program, name);
  } else {
    LOG_WARNING << "GLShaderProgram::attributeLocation(" << std::string(name) << "): shader program is not linked";
    return -1;
  }
}

int
GLShaderProgram::uniformLocation(const char* name)
{
  if (m_isLinked && m_program) {
    return glGetUniformLocation(m_program, name);
  } else {
    LOG_WARNING << "GLShaderProgram::uniformLocation(" << std::string(name) << "): shader program is not linked";
    return -1;
  }
}

void
GLShaderProgram::enableAttributeArray(int location)
{
  if (location != -1) {
    glEnableVertexAttribArray(location);
  }
}

void
GLShaderProgram::disableAttributeArray(int location)
{
  if (location != -1) {
    glDisableVertexAttribArray(location);
  }
}

void
GLShaderProgram::setAttributeArray(int location, const GLfloat* values, int tupleSize, int stride)
{
  if (location != -1) {
    glVertexAttribPointer(location, tupleSize, GL_FLOAT, GL_FALSE, stride, values);
  }
}

bool
GLShaderProgram::bind()
{
  if (!m_program)
    return false;
  if (!m_isLinked && !link())
    return false;

  glUseProgram(m_program);
  return true;
}

void
GLShaderProgram::release()
{
  glUseProgram(0);
}

void
GLShaderProgram::utilMakeSimpleProgram(std::string const& vertexShaderSource,
                                       std::string const& fragmentShaderSource,
                                       GLShader** outVShader,
                                       GLShader** outFShader)
{
  auto vshader = new GLShader(GL_VERTEX_SHADER);
  vshader->compileSourceCode(vertexShaderSource.c_str());

  if (!vshader->isCompiled()) {
    LOG_ERROR << "GLShaderProgram: Failed to compile vertex shader\n" << vshader->log();
  }

  auto fshader = new GLShader(GL_FRAGMENT_SHADER);
  fshader->compileSourceCode(fragmentShaderSource.c_str());

  if (!fshader->isCompiled()) {
    LOG_ERROR << "GLShaderProgram: Failed to compile fragment shader\n" << fshader->log();
  }

  addShader(vshader);
  addShader(fshader);
  link();

  if (!isLinked()) {
    LOG_ERROR << "GLShaderProgram: Failed to link shader program\n" << log();
  }

  if (outVShader != nullptr) {
    *outVShader = vshader;
  } else {
    delete vshader;
  }
  if (outFShader != nullptr) {
    *outFShader = fshader;
  } else {
    delete fshader;
  }
}

void
clearFramebuffer(const Scene* scene)
{
  // ready to start drawing; clear our main framebuffer
  if (scene) {
    glClearColor(scene->m_material.m_backgroundColor[0],
                 scene->m_material.m_backgroundColor[1],
                 scene->m_material.m_backgroundColor[2],
                 0.0);
  } else {
    glClearColor(0.0, 0.0, 0.0, 0.0);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}