#pragma once

#include "AppScene.h"
#include "glsl/GLBasicVolumeShader.h"
#include <memory>

class ImageXYZC;
class RenderSettings;

/**
 * 2D (xy) image renderer.
 *
 * Draws the specified image, using a user-selectable plane.
 *
 * The render is greyscale with a per-channel min/max for linear
 * contrast.
 */
class Image3D
{

public:
  /**
   * Create a 2D image.
   *
   * The size and position will be taken from the specified image.
   *
   * @param reader the image reader.
   * @param series the image series.
   * @param parent the parent of this object.
   */
  explicit Image3D();

  /// Destructor.
  virtual ~Image3D();

  void create(std::shared_ptr<ImageXYZC> img);

  void render(const CCamera& camera, const Scene* scene, const RenderSettings* renderSettings);

  void prepareTexture(Scene& s, bool useLinearInterpolation);

protected:
  /**
   * Set the size of the x and y dimensions.
   *
   * @param xlim the x axis limits (range).
   * @param ylim the y axis limits (range).
   */
  virtual void setSize(const glm::vec2& xlim, const glm::vec2& ylim);

  /// The vertex array.
  GLuint m_vertices; // vao
  /// The image vertices.
  GLuint m_image_vertices; // buffer
  /// The image elements.
  GLuint m_image_elements; // buffer
  size_t m_num_image_elements;
  /// The identifier of the texture owned and used by this object.
  unsigned int m_textureid;

private:
  /// The shader program for image rendering.
  GLBasicVolumeShader* m_image3d_shader;

  uint8_t* m_fusedrgbvolume;
};
