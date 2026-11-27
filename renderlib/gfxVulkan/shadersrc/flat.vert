#version 450

layout(set = 0, binding = 0, std140) uniform FlatVertParams
{
  vec4 colour;
  mat4 mvp;
}
flatVertParams;

layout(location = 0) in vec3 position;

layout(location = 0) out VertexData
{
  vec4 f_colour;
}
outData;

void
main()
{
  gl_Position = flatVertParams.mvp * vec4(position, 1.0);
  outData.f_colour = flatVertParams.colour;
}
