#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in VertexData
{
  vec2 f_texcoord;
} inData;

layout(location = 0) out vec4 outputColour;

void main() {
  vec4 texval = texture(tex, inData.f_texcoord);

  outputColour = texval;
}
