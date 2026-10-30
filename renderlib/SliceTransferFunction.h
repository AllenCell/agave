#pragma once

#include "AppScene.h"
#include "ImageXYZC.h"

#include <algorithm>
#include <array>
#include <cstdint>

// Compact, backend-neutral description of the first four enabled volume
// channels. Slice renderers use this to keep OpenGL and Vulkan transfer-
// function behaviour in lockstep without depending on path-tracing shaders.
struct SliceTransferFunction
{
  static constexpr int MAX_CHANNELS = 4;
  static constexpr int MAX_NODES = 16;

  int channelCount = 0;
  std::array<uint32_t, MAX_CHANNELS> sourceChannels = { 0, 0, 0, 0 };
  glm::vec4 intensityMin = glm::vec4(0.0f);
  glm::vec4 intensityMax = glm::vec4(1.0f);
  glm::vec4 lutMin = glm::vec4(0.0f);
  glm::vec4 lutMax = glm::vec4(1.0f);
  glm::vec4 labels = glm::vec4(0.0f);
  glm::vec4 opacity = glm::vec4(0.0f);
  std::array<glm::vec4, MAX_CHANNELS> diffuse = {
    glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(1.0f)
  };
  glm::ivec4 nodeCounts = glm::ivec4(0);
  // vec4 is intentional: std140 arrays of vec2 have a 16-byte stride. Keeping
  // each (x, y) node in a vec4 makes this directly uploadable to Vulkan and GL.
  std::array<glm::vec4, MAX_CHANNELS * MAX_NODES> nodes = {};
};

inline SliceTransferFunction
buildSliceTransferFunction(const Scene& scene)
{
  SliceTransferFunction result;
  if (!scene.m_volume || scene.m_volume->sizeC() == 0) {
    return result;
  }

  const uint32_t channelTotal = static_cast<uint32_t>(scene.m_volume->sizeC());
  for (uint32_t channel = 0;
       channel < channelTotal && result.channelCount < SliceTransferFunction::MAX_CHANNELS;
       ++channel) {
    if (channel >= MAX_CPU_CHANNELS || !scene.m_material.m_enabled[channel]) {
      continue;
    }

    const int active = result.channelCount++;
    result.sourceChannels[active] = channel;
    const Channelu16* volumeChannel = scene.m_volume->channel(channel);
    const float dataMin = static_cast<float>(volumeChannel->m_histogram.getDataMin());
    const float dataMax = static_cast<float>(volumeChannel->m_histogram.getDataMax());
    result.intensityMin[active] = dataMin;
    result.intensityMax[active] = std::max(dataMax, dataMin + 1.0f);

    uint16_t gradientMin = 0;
    uint16_t gradientMax = 0;
    const bool hasGradientRange =
      scene.m_material.m_gradientData[channel].getMinMax(volumeChannel->m_histogram, &gradientMin, &gradientMax);
    result.lutMin[active] = hasGradientRange ? static_cast<float>(gradientMin) : dataMin;
    result.lutMax[active] = hasGradientRange ? static_cast<float>(gradientMax) : dataMax;
    result.lutMax[active] = std::max(result.lutMax[active], result.lutMin[active] + 1.0f);

    result.labels[active] = scene.m_material.m_labels[channel];
    result.opacity[active] = scene.m_material.m_opacity[channel];
    result.diffuse[active] = glm::vec4(scene.m_material.m_diffuse[channel * 3 + 0],
                                       scene.m_material.m_diffuse[channel * 3 + 1],
                                       scene.m_material.m_diffuse[channel * 3 + 2],
                                       1.0f);

    const auto controlPoints =
      scene.m_material.m_gradientData[channel].getControlPoints(volumeChannel->m_histogram);
    const int nodeCount = std::min(static_cast<int>(controlPoints.size()), SliceTransferFunction::MAX_NODES);
    result.nodeCounts[active] = nodeCount;
    for (int node = 0; node < nodeCount; ++node) {
      result.nodes[active * SliceTransferFunction::MAX_NODES + node] =
        glm::vec4(controlPoints[node].first, controlPoints[node].second, 0.0f, 0.0f);
    }
  }

  // Both existing raw-volume uploaders fill unused components with a valid
  // channel. Only channelCount is shaded, but repeating the last source keeps
  // upload code safe when fewer than four channels are enabled.
  const uint32_t fallback = result.channelCount > 0 ? result.sourceChannels[result.channelCount - 1] : 0u;
  for (int active = result.channelCount; active < SliceTransferFunction::MAX_CHANNELS; ++active) {
    result.sourceChannels[active] = fallback;
  }
  return result;
}

