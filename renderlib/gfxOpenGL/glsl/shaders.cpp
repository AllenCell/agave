#include "shaders.h"

#include "Logging.h"

#include "shadersrc/basicVolume_frag_gen.hpp"
#include "shadersrc/basicVolume_vert_gen.hpp"
#include "shadersrc/copy_frag_gen.hpp"
#include "shadersrc/copy_vert_gen.hpp"
#include "shadersrc/flat_frag_gen.hpp"
#include "shadersrc/flat_vert_gen.hpp"
#include "shadersrc/gui_frag_gen.hpp"
#include "shadersrc/gui_vert_gen.hpp"
#include "shadersrc/imageNoLut_frag_gen.hpp"
#include "shadersrc/imageNoLut_vert_gen.hpp"
#include "shadersrc/pathTraceVolume_frag_gen.hpp"
#include "shadersrc/pathTraceVolume_vert_gen.hpp"
#include "shadersrc/ptAccum_frag_gen.hpp"
#include "shadersrc/ptAccum_vert_gen.hpp"
#include "shadersrc/thickLines_frag_gen.hpp"
#include "shadersrc/thickLines_vert_gen.hpp"
#include "shadersrc/toneMap_frag_gen.hpp"
#include "shadersrc/toneMap_vert_gen.hpp"

#include <map>

std::map<std::string, std::string> shaderSources = {
  { "basicVolume_frag", basicVolume_frag_src },
  { "basicVolume_vert", basicVolume_vert_src },
  { "copy_frag", copy_frag_src },
  { "copy_vert", copy_vert_src },
  { "flat_frag", flat_frag_src },
  { "flat_vert", flat_vert_src },
  { "gui_frag", gui_frag_src },
  { "gui_vert", gui_vert_src },
  { "imageNoLut_frag", imageNoLut_frag_src },
  { "imageNoLut_vert", imageNoLut_vert_src },
  { "pathTraceVolume_frag", pathTraceVolume_frag_src },
  { "pathTraceVolume_vert", pathTraceVolume_vert_src },
  { "ptAccum_frag", ptAccum_frag_src },
  { "ptAccum_vert", ptAccum_vert_src },
  { "thickLines_frag", thickLines_frag_src },
  { "thickLines_vert", thickLines_vert_src },
  { "toneMap_frag", toneMap_frag_src },
  { "toneMap_vert", toneMap_vert_src },
};

const std::string
getShaderSource(const std::string& shaderName)
{
  if (shaderSources.find(shaderName) != shaderSources.end()) {
    return shaderSources[shaderName];
  }
  LOG_ERROR << "Shader source not found: " << shaderName;
  return "";
}