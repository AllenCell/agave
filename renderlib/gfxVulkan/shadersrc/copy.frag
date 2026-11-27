#version 450

layout(set = 0, binding = 0) uniform sampler2D tTexture0;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 out_FragColor;

void main()
{
  out_FragColor = texture(tTexture0, vUv);
}
