#pragma once

#include "json/json.hpp"

#include <memory>
#include <string>

class ImageXYZC;
struct VolumeDimensions;

// Build the JSON description of a freshly loaded volume.
//
// Every code path that loads a volume reports it through this one function --
// LoadDataCommand over the binary protocol, PythonRenderer in process -- so the
// structure clients see cannot drift between them.
//
// The top-level fields describe the volume that was actually loaded, after any
// channel, region, or multiresolution-level subsetting. The nested
// "volume_dimensions" object describes the source as the reader reported it,
// which may be larger. For an in-memory array the two always agree.
//
// `image` is the loaded volume, `dims` the source metadata, and `name` the
// file path or array name it came from.
nlohmann::json
volumeInfoToJson(const std::shared_ptr<ImageXYZC>& image, const VolumeDimensions& dims, const std::string& name);
