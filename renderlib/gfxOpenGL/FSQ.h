#pragma once

#include "Image2D.h"

/**
 * 2D (xy) image renderer.
 *
 * Draws the specified image, using a user-selectable plane.
 *
 * The render is greyscale with a per-channel min/max for linear
 * contrast.
 */
class FSQ : public Image2D
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
  explicit FSQ();

  /// Destructor.
  ~FSQ() override;

  void render(const glm::mat4& mvp) override;

private:
};
