#version 400 core

layout(location = 0) in vec2 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
out VertexData
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
