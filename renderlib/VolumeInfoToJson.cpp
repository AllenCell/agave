#include "VolumeInfoToJson.h"

#include "ImageXYZC.h"
#include "VolumeDimensions.h"

nlohmann::json
buildVolumeInfoJson(const std::shared_ptr<ImageXYZC>& image, const VolumeDimensions& dims, const std::string& name)
{
  nlohmann::json j;
  j["name"] = name;

  // the volume as loaded
  j["x"] = (int)image->sizeX();
  j["y"] = (int)image->sizeY();
  j["z"] = (int)image->sizeZ();
  j["c"] = (int)image->sizeC();
  j["t"] = (int)dims.sizeT;
  j["pixel_size_x"] = image->physicalSizeX();
  j["pixel_size_y"] = image->physicalSizeY();
  j["pixel_size_z"] = image->physicalSizeZ();
  j["spatial_units"] = image->spatialUnits();

  std::vector<std::string> channelNames;
  std::vector<uint16_t> channelMinIntensity;
  std::vector<uint16_t> channelMaxIntensity;
  channelNames.reserve(image->sizeC());
  channelMinIntensity.reserve(image->sizeC());
  channelMaxIntensity.reserve(image->sizeC());
  for (uint32_t i = 0; i < image->sizeC(); ++i) {
    channelNames.push_back(image->channel(i)->m_name);
    channelMinIntensity.push_back(image->channel(i)->m_histogram.getDataMin());
    channelMaxIntensity.push_back(image->channel(i)->m_histogram.getDataMax());
  }
  j["channel_names"] = channelNames;
  j["channel_min_intensity"] = channelMinIntensity;
  j["channel_max_intensity"] = channelMaxIntensity;

  // full metadata for the source, which may describe more than was loaded
  nlohmann::json vd;
  vd["size_x"] = (int)dims.sizeX;
  vd["size_y"] = (int)dims.sizeY;
  vd["size_z"] = (int)dims.sizeZ;
  vd["size_c"] = (int)dims.sizeC;
  vd["size_t"] = (int)dims.sizeT;
  vd["physical_size_x"] = dims.physicalSizeX;
  vd["physical_size_y"] = dims.physicalSizeY;
  vd["physical_size_z"] = dims.physicalSizeZ;
  vd["spatial_units"] = dims.spatialUnits;
  vd["time_unit"] = dims.timeUnit;
  vd["time_units"] = dims.timeUnits;
  vd["bits_per_pixel"] = (int)dims.bitsPerPixel;
  vd["sample_format"] = (int)dims.sampleFormat;
  vd["dimension_order"] = dims.dimensionOrder;
  vd["channel_names"] = dims.channelNames;
  j["volume_dimensions"] = vd;

  return j;
}
