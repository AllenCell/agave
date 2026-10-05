#include "GLSliceShader.h"

#include "AppScene.h"
#include "CCamera.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "SliceTransferFunction.h"
#include "SliceViewState.h"
#include "gfxOpenGL/ImageXyzcGpu.h"
#include "gfxOpenGL/glsl/shaders.h"

#include <algorithm>

#include <glm/gtc/type_ptr.hpp>

namespace
{
struct SliceCameraTransform
{
  glm::vec2 pan = glm::vec2(0.0f);
  float zoom = 1.0f;
};

glm::vec4
toBottomLeftRect(const SliceRect& rect, int viewportHeight)
{
  return glm::vec4(rect.x,
                   static_cast<float>(viewportHeight) - rect.y - rect.height,
                   rect.width,
                   rect.height);
}

glm::vec2
slicePlaneWorldSize(SliceViewMode mode, const glm::vec3& boundsExtent)
{
  if (mode == SliceViewMode::X) {
    return glm::vec2(boundsExtent.z, boundsExtent.y);
  }
  if (mode == SliceViewMode::Y) {
    return glm::vec2(boundsExtent.x, boundsExtent.z);
  }
  return glm::vec2(boundsExtent.x, boundsExtent.y);
}

SliceCameraTransform
sliceCameraTransform(const Scene& scene,
                     const SliceViewState& state,
                     const CCamera& camera,
                     int viewportWidth,
                     int viewportHeight)
{
  SliceCameraTransform transform;
  if (!state.isSingleSlice() || camera.m_Projection != ORTHOGRAPHIC || viewportWidth <= 0 || viewportHeight <= 0) {
    return transform;
  }

  constexpr float MIN_EXTENT = 0.000001f;
  const glm::vec2 planeSize =
    glm::max(slicePlaneWorldSize(state.mode, scene.m_boundingBox.GetExtent()), glm::vec2(MIN_EXTENT));
  const float fitPixelsPerWorld =
    std::min(static_cast<float>(viewportWidth) / planeSize.x, static_cast<float>(viewportHeight) / planeSize.y);
  const float pixelsPerWorld =
    static_cast<float>(viewportHeight) / (2.0f * std::max(camera.m_OrthoScale, MIN_EXTENT));

  transform.zoom = pixelsPerWorld / fitPixelsPerWorld;
  const glm::vec3 centerOffset = scene.m_boundingBox.GetCenter() - camera.m_Target;
  transform.pan =
    glm::vec2(glm::dot(centerOffset, camera.m_U), glm::dot(centerOffset, camera.m_V)) * pixelsPerWorld;
  return transform;
}
}

GLSliceShader::GLSliceShader()
{
  m_vertexShader = new GLShader(GL_VERTEX_SHADER);
  m_vertexShader->compileSourceCode(getShaderSource("slice_vert").c_str());
  if (!m_vertexShader->isCompiled()) {
    LOG_ERROR << "GLSliceShader: failed to compile vertex shader\n" << m_vertexShader->log();
  }

  m_fragmentShader = new GLShader(GL_FRAGMENT_SHADER);
  m_fragmentShader->compileSourceCode(getShaderSource("slice_frag").c_str());
  if (!m_fragmentShader->isCompiled()) {
    LOG_ERROR << "GLSliceShader: failed to compile fragment shader\n" << m_fragmentShader->log();
  }

  addShader(m_vertexShader);
  addShader(m_fragmentShader);
  link();
  if (!isLinked()) {
    LOG_ERROR << "GLSliceShader: failed to link shader program\n" << log();
  }

  m_volumeTexture = uniformLocation("uVolumeTexture");
  m_transferTexture = uniformLocation("uTransferTexture");
  m_viewport = uniformLocation("uViewport");
  m_mode = uniformLocation("uMode");
  m_channelCount = uniformLocation("uChannelCount");
  m_indices = uniformLocation("uIndices");
  m_dimensions = uniformLocation("uDimensions");
  m_physicalDimensions = uniformLocation("uPhysicalDimensions");
  m_flipAxes = uniformLocation("uFlipAxes");
  m_pan = uniformLocation("uPan");
  m_zoom = uniformLocation("uZoom");
  m_paneXY = uniformLocation("uPaneXY");
  m_paneYZ = uniformLocation("uPaneYZ");
  m_paneXZ = uniformLocation("uPaneXZ");
  m_intensityMin = uniformLocation("uIntensityMin");
  m_intensityMax = uniformLocation("uIntensityMax");
  m_lutMin = uniformLocation("uLutMin");
  m_lutMax = uniformLocation("uLutMax");
  m_labels = uniformLocation("uLabels");
  m_opacity = uniformLocation("uOpacity");
  m_diffuse = uniformLocation("uDiffuse");
  m_tfNodeCounts = uniformLocation("uTfNodeCounts");
  m_tfNodes = uniformLocation("uTfNodes");
}

GLSliceShader::~GLSliceShader()
{
  delete m_fragmentShader;
  delete m_vertexShader;
}

void
GLSliceShader::setShadingUniforms(const Scene& scene,
                                  const SliceViewState& state,
                                  const CCamera& camera,
                                  int viewportWidth,
                                  int viewportHeight,
                                  const ImageGpu& image)
{
  ImageXYZC* volume = scene.m_volume.get();
  if (!volume) {
    return;
  }

  const SliceTransferFunction transfer = buildSliceTransferFunction(scene);
  const glm::ivec3 dimensions(static_cast<int>(volume->sizeX()),
                              static_cast<int>(volume->sizeY()),
                              static_cast<int>(volume->sizeZ()));
  const glm::ivec3 indices = glm::clamp(state.indices, glm::ivec3(0), dimensions - glm::ivec3(1));
  const glm::vec3 physicalDimensions = glm::max(volume->getPhysicalDimensions(), glm::vec3(0.000001f));
  const TripleSliceLayout layout =
    computeTripleSliceLayout(physicalDimensions, glm::ivec2(viewportWidth, viewportHeight), 2.0f);

  glUniform1i(m_volumeTexture, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, image.m_VolumeGLTexture);
  glUniform1i(m_transferTexture, 1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, image.m_ActiveChannelColormaps);

  glUniform2f(m_viewport, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
  glUniform1i(m_mode, static_cast<int>(state.mode));
  glUniform1i(m_channelCount, transfer.channelCount);
  glUniform3iv(m_indices, 1, glm::value_ptr(indices));
  glUniform3iv(m_dimensions, 1, glm::value_ptr(dimensions));
  glUniform3fv(m_physicalDimensions, 1, glm::value_ptr(physicalDimensions));
  const glm::vec3 flipAxes(volume->getVolumeAxesFlipped());
  glUniform3fv(m_flipAxes, 1, glm::value_ptr(flipAxes));
  const SliceCameraTransform cameraTransform =
    sliceCameraTransform(scene, state, camera, viewportWidth, viewportHeight);
  glUniform2fv(m_pan, 1, glm::value_ptr(cameraTransform.pan));
  glUniform1f(m_zoom, cameraTransform.zoom);

  const glm::vec4 paneXY = toBottomLeftRect(layout.xy, viewportHeight);
  const glm::vec4 paneYZ = toBottomLeftRect(layout.yz, viewportHeight);
  const glm::vec4 paneXZ = toBottomLeftRect(layout.xz, viewportHeight);
  glUniform4fv(m_paneXY, 1, glm::value_ptr(paneXY));
  glUniform4fv(m_paneYZ, 1, glm::value_ptr(paneYZ));
  glUniform4fv(m_paneXZ, 1, glm::value_ptr(paneXZ));

  glUniform4fv(m_intensityMin, 1, glm::value_ptr(transfer.intensityMin));
  glUniform4fv(m_intensityMax, 1, glm::value_ptr(transfer.intensityMax));
  glUniform4fv(m_lutMin, 1, glm::value_ptr(transfer.lutMin));
  glUniform4fv(m_lutMax, 1, glm::value_ptr(transfer.lutMax));
  glUniform4fv(m_labels, 1, glm::value_ptr(transfer.labels));
  glUniform4fv(m_opacity, 1, glm::value_ptr(transfer.opacity));
  glUniform4fv(m_diffuse, SliceTransferFunction::MAX_CHANNELS, glm::value_ptr(transfer.diffuse[0]));
  glUniform4iv(m_tfNodeCounts, 1, glm::value_ptr(transfer.nodeCounts));
  glUniform4fv(
    m_tfNodes, SliceTransferFunction::MAX_CHANNELS * SliceTransferFunction::MAX_NODES, glm::value_ptr(transfer.nodes[0]));
}
