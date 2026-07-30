#include "ApplyVolumeToScene.h"

#include "AppScene.h"
#include "GradientData.h"
#include "ImageXYZC.h"
#include "Logging.h"
#include "RenderSettings.h"

bool
applyVolumeToScene(Scene* scene, const std::shared_ptr<ImageXYZC>& image, RenderSettings* renderSettings)
{
  if (!scene || !image) {
    return false;
  }

  if (scene->m_volume) {
    // Every time step of a given source is expected to have the same channel
    // count -- only the voxel data changes. A mismatch therefore means the file
    // or our reading of it is wrong, not that we should adapt to it. Refuse the
    // volume rather than installing one whose surplus channels would keep
    // stale, un-remapped transfer functions, and rather than indexing past the
    // end of the outgoing volume's channels as this code used to.
    if (image->sizeC() != scene->m_volume->sizeC()) {
      LOG_ERROR << "Channel count mismatch for different times in same file: expected " << scene->m_volume->sizeC()
                << " but the new time has " << image->sizeC() << ". Refusing to apply it.";
      return false;
    }

    // Remap LUTs to preserve absolute thresholding across the change of volume.
    // Pairing is by index, which relies on channel order being identical between
    // time steps of the same source (see the header).
    for (uint32_t i = 0; i < image->sizeC(); ++i) {
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
