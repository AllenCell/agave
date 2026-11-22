#version 450

layout(location = 0) in vec2 position;
layout(set = 0, binding = 0, std140) uniform PtAccumVertParams
{
  mat4 modelViewMatrix;
  mat4 projectionMatrix;
}
ptAccumVertParams;
layout(location = 0) out VertexData
{
  vec2 pObj;
}
outData;

void
main()
{
  outData.pObj = position;
  gl_Position = vec4(position, 0.0, 1.0);
}
