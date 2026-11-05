#version 400 core

uniform sampler2D tex;

in VertexData
{
  vec2 f_texcoord;
} inData;

out vec4 outputColour;

void main() {
  vec4 texval = texture(tex, inData.f_texcoord);

  outputColour = texval;
}
