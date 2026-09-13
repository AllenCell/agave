#version 400 core

layout(location = 0) in vec2 coord2d;
layout(location = 1) in vec2 texcoord;
uniform mat4 mvp;

out VertexData
{
  vec2 f_texcoord;
}
outData;

void
main()
{
  gl_Position = mvp * vec4(coord2d, 0.0, 1.0);
  outData.f_texcoord = texcoord;
}
