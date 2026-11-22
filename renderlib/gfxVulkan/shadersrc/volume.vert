#version 450

layout(location = 0) in vec3 position;

layout(set = 0, binding = 0, std140) uniform VolumeParams
{
  mat4 modelViewMatrix;
  mat4 projectionMatrix;
  mat4 inverseModelViewMatrix;
  vec4 clipPlane;
  vec4 aabbMinMode;
  vec4 aabbMaxSteps;
  vec4 flipAxesPerspective;
  vec4 viewportDensity;
  vec4 colorParams;
  vec4 lutMin;
  vec4 lutMax;
  vec4 background;
}
u;

layout(location = 0) out vec3 pObj;

void
main()
{
  pObj = position;
  gl_Position = u.projectionMatrix * u.modelViewMatrix * vec4(position, 1.0);
}
