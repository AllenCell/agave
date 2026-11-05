// Generated C source file containing shader

#include <string>

const std::string gui_vert_chunk_0 = R"(
#version 400 core

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vCol;
layout(location = 3) in uint vCode;

uniform mat4 projection;
uniform int picking;

out vec4 Frag_color;
out vec2 Frag_UV;

void
main()
{
  Frag_UV = vUV;
  if (picking == 1) {
    Frag_color = vec4(
      float(vCode & 0xffu) / 255.0, float((vCode >> 8) & 0xffu) / 255.0, float((vCode >> 16) & 0xffu) / 255.0, 1.0);
  } else {
    Frag_color = vCol;
  }

  gl_Position = projection * vec4(vPos, 1.0);
}

)";

const std::string gui_vert_src = 
    gui_vert_chunk_0;
