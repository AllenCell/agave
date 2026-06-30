#version 450

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vCol;
layout(location = 3) in uint vCode;

layout(set = 0, binding = 0, std140) uniform GuiVertParams
{
  mat4 projection;
  int picking;
}
guiVertParams;

layout(location = 0) out vec4 Frag_color;
layout(location = 1) out vec2 Frag_UV;

void
main()
{
  Frag_UV = vUV;
  if (guiVertParams.picking == 1) {
    Frag_color = vec4(
      float(vCode & 0xffu) / 255.0, float((vCode >> 8) & 0xffu) / 255.0, float((vCode >> 16) & 0xffu) / 255.0, 1.0);
  } else {
    Frag_color = vCol;
  }

  gl_Position = guiVertParams.projection * vec4(vPos, 1.0);
}
