// Generated C source file containing shader

#include <string>

const std::string basicVolume_vert_chunk_0 = R"(
#version 400 core

layout(location = 0) in vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
out VertexData
{
  vec3 pObj;
}
outData;

void
main()
{
  outData.pObj = position;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}

)";

const std::string basicVolume_vert_src = 
    basicVolume_vert_chunk_0;
