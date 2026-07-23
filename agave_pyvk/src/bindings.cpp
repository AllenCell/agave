#include "renderlib/PythonRenderer.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <nanobind/nanobind.h>
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

} // namespace

NB_MODULE(_native, m)
{
  m.doc() = "Headless Vulkan bindings for AGAVE renderlib";

  nb::class_<PythonRenderer>(m, "PythonRenderer")
    .def(nb::init<const std::string&, const std::string&, int>(),
         nb::arg("mode") = "pathtrace",
         nb::arg("asset_path") = "",
         nb::arg("gpu") = 0)
    .def("execute", &execute)
    .def("close", &PythonRenderer::close);
}
