#version 400 core

uniform sampler2D tTexture0;
in vec2 vUv;
out vec4 out_FragColor;

void main()
{
  out_FragColor = texture(tTexture0, vUv);
}
