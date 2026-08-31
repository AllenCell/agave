#include "renderlib/PythonRenderer.h"
#include "renderlib/ImageXYZC.h"
#include "renderlib/VolumeDimensions.h"
#include "renderlib/gfxapi/Backend.h"
#include "renderlib/io/ConvertChannelData.h"
#include "renderlib/io/FileReader.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

namespace {

PythonRendererArguments
convertArguments(int commandId, const nb::args& args)
{
  const auto types = PythonRenderer::commandArgumentTypes(commandId);
  if (args.size() != types.size()) {
    throw PythonRendererArgumentError("command " + std::to_string(commandId) + " expects " +
                                      std::to_string(types.size()) + " arguments, got " + std::to_string(args.size()));
  }

  PythonRendererArguments converted;
  converted.reserve(args.size());
  for (size_t index = 0; index < types.size(); ++index) {
    switch (types[index]) {
      case CommandArgType::I32:
        converted.emplace_back(nb::cast<int32_t>(args[index]));
        break;
      case CommandArgType::F32:
        converted.emplace_back(nb::cast<float>(args[index]));
        break;
      case CommandArgType::STR:
        converted.emplace_back(nb::cast<std::string>(args[index]));
        break;
      case CommandArgType::F32A:
        converted.emplace_back(nb::cast<std::vector<float>>(args[index]));
        break;
      case CommandArgType::I32A:
        converted.emplace_back(nb::cast<std::vector<int32_t>>(args[index]));
        break;
    }
  }
  return converted;
}

nb::object
convertResult(PythonRendererResult result)
{
  if (std::holds_alternative<std::monostate>(result)) {
    return nb::none();
  }
  if (const auto* message = std::get_if<std::string>(&result)) {
    return nb::str(message->c_str());
  }

  const auto& image = std::get<PythonRendererImage>(result);
  return nb::make_tuple(
    image.width, image.height, nb::bytes(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size()));
}

nb::object
execute(PythonRenderer& renderer, int commandId, const nb::args& args)
{
  try {
    return convertResult(renderer.execute(commandId, convertArguments(commandId, args)));
  } catch (const PythonRendererArgumentError& error) {
    throw nb::type_error(error.what());
  } catch (const PythonRendererValueError& error) {
    throw nb::value_error(error.what());
  }
}

size_t
checkedProduct(size_t left, size_t right)
{
  if (right != 0 && left > std::numeric_limits<size_t>::max() / right) {
    throw nb::value_error("Array dimensions are too large");
  }
  return left * right;
}

// TODO should there be a command for this?
std::string
loadArray(PythonRenderer& renderer,
          const nb::ndarray<nb::numpy, nb::c_contig, nb::device::cpu>& array,
          const std::string& name,
          const std::vector<float>& voxelSize,
          const std::string& spatialUnits,
          std::vector<std::string> channelNames)
{
  if (array.ndim() != 3 && array.ndim() != 4) {
    throw nb::value_error("data must have ZYX or CZYX shape");
  }

  for (size_t axis = 0; axis < array.ndim(); ++axis) {
    if (array.shape(axis) == 0 || array.shape(axis) > std::numeric_limits<uint32_t>::max()) {
      throw nb::value_error("array dimensions must be nonzero and fit in uint32");
    }
  }
  if (voxelSize.size() != 3 || !std::isfinite(voxelSize[0]) || !std::isfinite(voxelSize[1]) ||
      !std::isfinite(voxelSize[2]) || voxelSize[0] <= 0.0f || voxelSize[1] <= 0.0f || voxelSize[2] <= 0.0f) {
    throw nb::value_error("voxel_size must contain three positive finite values");
  }

  VolumeDimensions dimensions;
  const size_t offset = array.ndim() == 4 ? 1 : 0;
  dimensions.sizeC = array.ndim() == 4 ? static_cast<uint32_t>(array.shape(0)) : 1;
  dimensions.sizeZ = static_cast<uint32_t>(array.shape(offset));
  dimensions.sizeY = static_cast<uint32_t>(array.shape(offset + 1));
  dimensions.sizeX = static_cast<uint32_t>(array.shape(offset + 2));
  dimensions.sizeT = 1;
  dimensions.physicalSizeX = voxelSize[0];
  dimensions.physicalSizeY = voxelSize[1];
  dimensions.physicalSizeZ = voxelSize[2];
  dimensions.spatialUnits = spatialUnits;
  dimensions.dimensionOrder = "XYZCT";

  if (dimensions.sizeC > 32) {
    throw nb::value_error("data may contain at most 32 channels");
  }
  if (channelNames.empty()) {
    channelNames.reserve(dimensions.sizeC);
    for (uint32_t channel = 0; channel < dimensions.sizeC; ++channel) {
      channelNames.push_back("Channel " + std::to_string(channel));
    }
  } else if (channelNames.size() != dimensions.sizeC) {
    throw nb::value_error("channel_names must match the array channel count");
  }
  dimensions.channelNames = channelNames;

  if (array.dtype() == nb::dtype<uint8_t>()) {
    dimensions.bitsPerPixel = 8;
    dimensions.sampleFormat = 1;
  } else if (array.dtype() == nb::dtype<uint16_t>()) {
    dimensions.bitsPerPixel = 16;
    dimensions.sampleFormat = 1;
  } else if (array.dtype() == nb::dtype<float>()) {
    dimensions.bitsPerPixel = 32;
    dimensions.sampleFormat = 3;
  } else {
    throw nb::type_error("data dtype must be uint8, uint16, or float32");
  }

  size_t voxelsPerChannel = checkedProduct(dimensions.sizeX, dimensions.sizeY);
  voxelsPerChannel = checkedProduct(voxelsPerChannel, dimensions.sizeZ);
  const size_t totalVoxels = checkedProduct(voxelsPerChannel, dimensions.sizeC);
  const size_t outputBytes = checkedProduct(totalVoxels, ImageXYZC::IN_MEMORY_BPP / 8);
  auto converted = std::make_unique<uint8_t[]>(outputBytes);

  const auto* source = static_cast<const uint8_t*>(array.data());
  const size_t sourceChannelBytes = checkedProduct(voxelsPerChannel, array.itemsize());
  const size_t outputChannelBytes = checkedProduct(voxelsPerChannel, ImageXYZC::IN_MEMORY_BPP / 8);
  for (uint32_t channel = 0; channel < dimensions.sizeC; ++channel) {
    if (!FileReaderUtil::convertChannelData(
          converted.get() + channel * outputChannelBytes, source + channel * sourceChannelBytes, dimensions)) {
      throw nb::value_error("data could not be converted to AGAVE's uint16 volume format");
    }
  }

  std::vector<uint32_t> shape = { dimensions.sizeC, dimensions.sizeZ, dimensions.sizeY, dimensions.sizeX };
  auto image = FileReader::loadFromArray_4D(
    std::move(converted), shape, name, { 'C', 'Z', 'Y', 'X' }, channelNames, voxelSize, spatialUnits, false);
  return renderer.loadVolume(std::move(image), dimensions, name);
}

} // namespace

NB_MODULE(_native, m)
{
  m.doc() = "Headless Vulkan bindings for AGAVE renderlib";

  nb::class_<PythonRenderer>(m, "PythonRenderer")
    .def(nb::init<const std::string&, const std::string&, int>(),
         nb::arg("mode") = "pathtrace",
         nb::arg("asset_path") = "",
         // Auto-selects by default; an explicit index is used as given and
         // validated rather than silently replaced.
         nb::arg("gpu") = gfxApi::kAutoSelectGpu)
    .def("execute", &execute)
    .def("load_array",
         &loadArray,
         nb::arg("data"),
         nb::arg("name") = "array",
         nb::arg("voxel_size") = std::vector<float>{ 1.0f, 1.0f, 1.0f },
         nb::arg("spatial_units") = "units",
         nb::arg("channel_names") = std::vector<std::string>{})
    .def("close", &PythonRenderer::close);
}
