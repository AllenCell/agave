#include "ApplyVolumeToScene.h"

#include "AppScene.h"
#include "GradientData.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "RenderSettings.h"

#include <algorithm>

bool
applyVolumeToScene(Scene* scene, const std::shared_ptr<ImageXYZC>& image, RenderSettings* renderSettings)
{
  if (!scene || !image) {
    return false;
  }

  if (scene->m_volume) {
    // A time step is expected to keep the same channel configuration and
    // dimensions; only the voxel data changes. Warn rather than fail, matching
    // the behaviour SetTimeCommand had.
    if (image->sizeC() != scene->m_volume->sizeC()) {
      LOG_ERROR << "Channel count mismatch for different times in same file";
    }

    // Remap LUTs to preserve absolute thresholding across the change of volume.
    const uint32_t channels = std::min(image->sizeC(), scene->m_volume->sizeC());
    for (uint32_t i = 0; i < channels; ++i) {
      GradientData& lutInfo = scene->m_material.m_gradientData[i];
      lutInfo.convert(scene->m_volume->channel(i)->m_histogram, image->channel(i)->m_histogram);
      image->channel(i)->generateFromGradientData(lutInfo);
    }
  }

  // Now we are ready to lose the old channel histograms.
  scene->m_volume = image;

  if (renderSettings) {
    renderSettings->m_DirtyFlags.SetFlag(VolumeDataDirty);
    renderSettings->m_DirtyFlags.SetFlag(TransferFunctionDirty);
  }
  return true;
}
