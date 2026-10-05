// Generated C source file containing shader

#include <string>

const std::string flat_vert_chunk_0 = R"(
#version 400 core

uniform vec4 colour;
uniform mat4 mvp;

layout(location = 0) in vec3 position;

out VertexData
{
  vec4 f_colour;
}
outData;

void
main()
{
  gl_Position = mvp * vec4(position, 1.0);
  outData.f_colour = colour;
}

)";

const std::string flat_vert_src = 
    flat_vert_chunk_0;
