#version 450

layout(location = 0) in vec2 coord2d;
layout(location = 1) in vec2 texcoord;
layout(set = 0, binding = 0, std140) uniform ImageNoLutVertParams
{
  mat4 mvp;
}
imageNoLutVertParams;

layout(location = 0) out VertexData
{
  vec2 f_texcoord;
}
outData;

void
main()
{
  gl_Position = imageNoLutVertParams.mvp * vec4(coord2d, 0.0, 1.0);
  outData.f_texcoord = texcoord;
}
