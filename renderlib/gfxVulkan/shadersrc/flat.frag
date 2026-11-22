#version 450

layout(location = 0) in VertexData
{
  vec4 f_colour;
} inData;

layout(location = 0) out vec4 outputColour;

void main() {
  outputColour = inData.f_colour;
}
