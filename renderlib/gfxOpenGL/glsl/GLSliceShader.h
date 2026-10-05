#pragma once

#include "gfxOpenGL/Util.h"

struct ImageGpu;
class CCamera;
class Scene;
struct SliceViewState;

class GLSliceShader : public GLShaderProgram
{
public:
  GLSliceShader();
  ~GLSliceShader();

  void setShadingUniforms(const Scene& scene,
                          const SliceViewState& state,
                          const CCamera& camera,
                          int viewportWidth,
                          int viewportHeight,
                          const ImageGpu& image);

private:
  GLShader* m_vertexShader = nullptr;
  GLShader* m_fragmentShader = nullptr;

  int m_volumeTexture = -1;
  int m_transferTexture = -1;
  int m_viewport = -1;
  int m_mode = -1;
  int m_channelCount = -1;
  int m_indices = -1;
  int m_dimensions = -1;
  int m_physicalDimensions = -1;
  int m_flipAxes = -1;
  int m_pan = -1;
  int m_zoom = -1;
  int m_paneXY = -1;
  int m_paneYZ = -1;
  int m_paneXZ = -1;
  int m_intensityMin = -1;
  int m_intensityMax = -1;
  int m_lutMin = -1;
  int m_lutMax = -1;
  int m_labels = -1;
  int m_opacity = -1;
  int m_diffuse = -1;
  int m_tfNodeCounts = -1;
  int m_tfNodes = -1;
};
