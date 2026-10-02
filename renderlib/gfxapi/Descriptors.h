#pragma once

#include "Handles.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gfxApi {

enum class ShaderStage : uint8_t
{
  Vertex,
  Fragment,
  Geometry,
  Compute,
};

// Source kind hints to the backend how to consume `code`.
// OpenGL backend currently only accepts GLSL.
enum class ShaderSourceKind : uint8_t
{
  GLSL,
  SPIRV,
  WGSL,
};

struct ShaderDesc
{
  ShaderStage stage = ShaderStage::Vertex;
  ShaderSourceKind sourceKind = ShaderSourceKind::GLSL;
  std::string source;
  // Optional human-readable name for diagnostics.
  std::string debugName;
};

struct ShaderProgramDesc
{
  // Shaders linked into the program. For typical graphics use,
  // expect {vertex, fragment}.
  std::vector<ShaderHandle> shaders;
  std::string debugName;
};

} // namespace gfxApi
