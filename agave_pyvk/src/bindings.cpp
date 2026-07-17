#include "VulkanRenderer.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace nb = nanobind;

NB_MODULE(_native, m)
{
  m.doc() = "Headless Vulkan bindings for AGAVE renderlib";

  nb::class_<VulkanRenderer>(m, "VulkanRenderer")
    .def(nb::init<const std::string&, const std::string&, int>(),
         nb::arg("mode") = "pathtrace",
         nb::arg("asset_path") = "",
         nb::arg("gpu") = 0)
    .def("execute", &VulkanRenderer::execute)
    .def("close", &VulkanRenderer::close);
}
