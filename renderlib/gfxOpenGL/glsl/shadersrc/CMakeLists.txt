# this list must match the sources in make_shaders.py

set(SHADERS_CPP
"basicVolume_frag_gen.hpp"
"basicVolume_vert_gen.hpp"
"copy_frag_gen.hpp"
"copy_vert_gen.hpp"
"flat_frag_gen.hpp"
"flat_vert_gen.hpp"
"gui_frag_gen.hpp"
"gui_vert_gen.hpp"
"imageNoLut_frag_gen.hpp"
"imageNoLut_vert_gen.hpp"
"pathTraceVolume_frag_gen.hpp"
"pathTraceVolume_vert_gen.hpp"
"ptAccum_frag_gen.hpp"
"ptAccum_vert_gen.hpp"
"thickLines_frag_gen.hpp"
"thickLines_vert_gen.hpp"
"toneMap_frag_gen.hpp"
"toneMap_vert_gen.hpp"
)

# Add the generated C++ file to your target
target_sources(renderlib PRIVATE ${SHADERS_CPP})
