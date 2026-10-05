#include "Image2D.h"
#include "Util.h"

#include "ImageXYZC.h"
#include "glad/glad.h"

#include <array>
#include <iostream>

Image2D::Image2D()
  : m_vertices(0)
  , m_image_vertices(0)
  , m_image_texcoords(0)
  , m_image_elements(0)
  , m_num_image_elements(0)
{
}

Image2D::~Image2D() {}

void
Image2D::create()
{
}

void
Image2D::setSize(const glm::vec2& xlim, const glm::vec2& ylim)
{
  const std::array<GLfloat, 12> square_vertices{ xlim[0], ylim[0], 0, xlim[1], ylim[0], 0,
                                                 xlim[1], ylim[1], 0, xlim[0], ylim[1], 0 };

  if (m_vertices == 0) {
    glGenVertexArrays(1, &m_vertices);
  }
  glBindVertexArray(m_vertices);

  if (m_image_vertices == 0) {
    glGenBuffers(1, &m_image_vertices);
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_image_vertices);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * square_vertices.size(), square_vertices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);

  glm::vec2 texxlim(0.0, 1.0);
  glm::vec2 texylim(0.0, 1.0);
  std::array<GLfloat, 8> square_texcoords{ texxlim[0], texylim[0], texxlim[1], texylim[0],
                                           texxlim[1], texylim[1], texxlim[0], texylim[1] };

  if (m_image_texcoords == 0) {
    glGenBuffers(1, &m_image_texcoords);
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_image_texcoords);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * square_texcoords.size(), square_texcoords.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(1);

  std::array<GLushort, 6> square_elements{ // front
                                           0, 1, 2, 2, 3, 0
  };

  if (m_image_elements == 0) {
    glGenBuffers(1, &m_image_elements);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_image_elements);
  glBufferData(
    GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * square_elements.size(), square_elements.data(), GL_STATIC_DRAW);
  m_num_image_elements = square_elements.size();
}

void
Image2D::destroy()
{
  glDeleteBuffers(1, &m_image_elements);
  glDeleteBuffers(1, &m_image_texcoords);
  glDeleteBuffers(1, &m_image_vertices);
  glDeleteVertexArrays(1, &m_vertices);
}
