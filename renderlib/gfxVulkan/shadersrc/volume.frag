#version 450

layout(location = 0) in vec3 pObj;
layout(location = 0) out vec4 outputColor;

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

layout(set = 0, binding = 1) uniform sampler3D volumeTexture;
layout(set = 0, binding = 2) uniform sampler2DArray transferTexture;

float
rand(vec2 co)
{
  float threadId = gl_FragCoord.x / (gl_FragCoord.y + 1.0);
  float bigVal = threadId * 1299721.0 / 911.0;
  vec2 smallVal = vec2(threadId * 7927.0 / 577.0, threadId * 104743.0 / 1039.0);
  return fract(sin(dot(co, smallVal)) * bigVal);
}

vec3
volumeCoord(vec3 pos)
{
  vec3 coord = pos + vec3(0.5);
  vec3 flipped = vec3(1.0) - coord;
  return mix(coord, flipped, lessThan(u.flipAxesPerspective.xyz, vec3(0.0)));
}

vec4
lumaToAlpha(vec4 color)
{
  float x = max(color.r, max(color.g, color.b));
  float denom = max(u.colorParams.z - u.colorParams.y, 0.00001);
  float xi = clamp((x - u.colorParams.y) / denom, 0.0, 1.0);
  color.a = clamp(pow(xi, u.colorParams.w), 0.0, 1.0);
  return color;
}

vec4
sampleTransfer(float raw, int layer)
{
  float denom = max(u.lutMax[layer] - u.lutMin[layer], 1.0 / 65535.0);
  float t = clamp((raw - u.lutMin[layer]) / denom, 0.0, 1.0);
  return texture(transferTexture, vec3(t, 0.5, float(layer)));
}

vec4
sampleVolume(vec3 pos)
{
  vec3 coord = volumeCoord(pos);
  bvec3 outside = bvec3(any(lessThan(coord, vec3(0.001))), any(greaterThan(coord, vec3(0.999))), false);
  if (outside.x || outside.y) {
    return vec4(0.0);
  }

  vec4 texel = textureLod(volumeTexture, coord, 0.0);
  int mode = int(u.aabbMinMode.w + 0.5);
  if (mode == 0) {
    return lumaToAlpha(vec4(texel.rgb, 1.0));
  }

  vec4 c0 = sampleTransfer(texel.r, 0);
  vec4 c1 = sampleTransfer(texel.g, 1);
  vec4 c2 = sampleTransfer(texel.b, 2);
  vec4 c3 = sampleTransfer(texel.a, 3);
  vec3 rgb = c0.rgb * c0.a + c1.rgb * c1.a + c2.rgb * c2.a + c3.rgb * c3.a;
  float alpha = clamp(c0.a + c1.a + c2.a + c3.a, 0.0, 1.0);
  return vec4(clamp(rgb, 0.0, 1.0), alpha);
}

bool
intersectBox(in vec3 rayOrigin, in vec3 rayDir, in vec3 boxMin, in vec3 boxMax, out float tnear, out float tfar)
{
  vec3 invR = vec3(1.0) / rayDir;
  vec3 tbot = invR * (boxMin - rayOrigin);
  vec3 ttop = invR * (boxMax - rayOrigin);
  vec3 tmin = min(ttop, tbot);
  vec3 tmax = max(ttop, tbot);
  tnear = max(max(tmin.x, tmin.y), tmin.z);
  tfar = min(min(tmax.x, tmax.y), tmax.z);

  float denom = dot(rayDir, u.clipPlane.xyz);
  if (length(u.clipPlane.xyz) > 0.0 && abs(denom) > 0.0001) {
    float tClip = dot(u.clipPlane.xyz * (-u.clipPlane.w) - rayOrigin, u.clipPlane.xyz) / denom;
    if (denom < 0.0) {
      tnear = max(tnear, tClip);
    } else {
      tfar = min(tfar, tClip);
    }
  }

  return tfar > tnear;
}

vec4
integrateVolume(vec3 eyeOrigin, vec3 eyeDir, float tnear, float tfar)
{
  vec4 accum = vec4(0.0);
  int steps = clamp(int(u.aabbMaxSteps.w + 0.5), 1, 768);
  float stepSize = 1.0 / float(steps);
  float jitterSeed = u.colorParams.y;
  float jitter =
    (rand(eyeDir.xy + gl_FragCoord.xy + vec2(jitterSeed, jitterSeed * 1.6180339)) - 0.5) * stepSize;
  float t = max(tnear, 0.0) + jitter;
  float sampleScale = 0.5 * 512.0 / float(steps);

  for (int i = 0; i < 768; ++i) {
    if (i >= steps || t > tfar || accum.a >= 0.995) {
      break;
    }

    vec3 pos = eyeOrigin + eyeDir * t;
    vec4 color = sampleVolume(pos);
    color.rgb *= u.colorParams.x;
    color.a *= u.viewportDensity.w;
    color.a = 1.0 - pow(max(1.0 - color.a, 0.0), sampleScale);
    color.rgb *= color.a;
    color = clamp(color, 0.0, 1.0);

    accum.rgb += (1.0 - accum.a) * color.rgb;
    accum.a += (1.0 - accum.a) * color.a;
    t += stepSize;
  }

  return accum;
}

void
main()
{
  vec2 uv = gl_FragCoord.xy / u.viewportDensity.xy;
  vec3 eyeOrigin;
  vec3 eyeDir;

  if (u.flipAxesPerspective.w != 0.0) {
    eyeOrigin = (u.inverseModelViewMatrix * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    eyeDir = normalize(pObj - eyeOrigin);
  } else {
    float zDist = 2.0;
    eyeDir = normalize((u.inverseModelViewMatrix * vec4(0.0, 0.0, -zDist, 0.0)).xyz);
    vec4 rayOrigin = vec4(2.0 * uv - 1.0, 1.0, 1.0);
    rayOrigin.xy *= u.viewportDensity.z;
    rayOrigin.x *= u.viewportDensity.x / u.viewportDensity.y;
    eyeOrigin = (u.inverseModelViewMatrix * rayOrigin).xyz;
  }

  float tnear = 0.0;
  float tfar = 0.0;
  if (!intersectBox(eyeOrigin, eyeDir, u.aabbMinMode.xyz, u.aabbMaxSteps.xyz, tnear, tfar)) {
    outputColor = vec4(0.0);
    return;
  }

  outputColor = integrateVolume(eyeOrigin, eyeDir, tnear, tfar);
}
