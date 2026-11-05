#pragma once

#include <memory>

#include <glad/glad.h>

#include "glm.h"

/**
 * 2D (xy) image renderer.
 *
 * Draws the specified image, using a user-selectable plane.
 *
 * The render is greyscale with a per-channel min/max for linear
 * contrast.
 */
class Image2D
{
public:
  Image2D();

  /// Destructor.
  virtual ~Image2D() = 0;

  /**
   * Create GL buffers.
   *
   * @note Requires a valid GL context.  Must be called before
   * rendering.
   */
  virtual void create();

  /**
   * Set the size of the x and y dimensions.
   *
   * @param xlim the x axis limits (range).
   * @param ylim the y axis limits (range).
   */
  virtual void setSize(const glm::vec2& xlim, const glm::vec2& ylim);

  /**
   * Render the image.
   *
   * @param mvp the model view projection matrix.
   */
  virtual void render(const glm::mat4& mvp) = 0;

protected:
  void destroy();

  /// The vertex array.
  GLuint m_vertices; // vao
  /// The image vertices.
  GLuint m_image_vertices; // buffer
  /// The image texture coordinates.
  GLuint m_image_texcoords; // buffer
  /// The image elements.
  GLuint m_image_elements; // buffer
  size_t m_num_image_elements;
};
