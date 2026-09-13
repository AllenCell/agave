#version 400 core

in VertexData
{
  vec3 pObj;
}
inData;

out vec4 outputColour;

uniform mat4 inverseModelViewMatrix;

uniform sampler3D textureAtlas;
uniform sampler2D textureAtlasMask;
// uniform sampler2D lut;

#define M_PI 3.14159265358979323846
uniform vec2 iResolution;
uniform float isPerspective;
uniform float orthoScale;
uniform float GAMMA_MIN;
uniform float GAMMA_MAX;
uniform float GAMMA_SCALE;
uniform float BRIGHTNESS;
uniform float DENSITY;
uniform float maskAlpha;
uniform int BREAK_STEPS;
uniform vec3 AABB_CLIP_MIN;
uniform vec3 AABB_CLIP_MAX;
uniform vec3 flipVolumeAxes;

uniform float dataRangeMin; // 0..1 (mapped from 0..uint16_max)
uniform float dataRangeMax; // 0..1 (mapped from 0..uint16_max)

uniform vec4 g_clipPlane;

float
powf(float a, float b)
{
  return pow(a, b);
}

float
rand(vec2 co)
{
  float threadId = gl_FragCoord.x / (gl_FragCoord.y + 1.0);
  float bigVal = threadId * 1299721.0 / 911.0;
  vec2 smallVal = vec2(threadId * 7927.0 / 577.0, threadId * 104743.0 / 1039.0);
  return fract(sin(dot(co, smallVal)) * bigVal);
}

vec4
luma2Alpha(vec4 color, float vmin, float vmax, float C)
{
  float x = max(color[2], max(color[0], color[1]));
  float xi = (x - vmin) / (vmax - vmin);
  xi = clamp(xi, 0.0, 1.0);
  float y = pow(xi, C);
  y = clamp(y, 0.0, 1.0);
  color[3] = y;
  return (color);
}

vec4
sampleAs3DTexture(sampler3D tex, vec4 pos)
{
  float bounds =
    float(pos[0] > 0.001 && pos[0] < 0.999 && pos[1] > 0.001 && pos[1] < 0.999 && pos[2] > 0.001 && pos[2] < 0.999);

  vec4 texval = textureLod(tex, pos.xyz * flipVolumeAxes, 0).rgba;
  vec4 retval = vec4(texval.rgb, 1.0);

  //    float texval = textureLod(tex, pos.xyz, 0).r;
  //	texval = (texval - dataRangeMin) / (dataRangeMax - dataRangeMin);
  //	vec4 retval = vec4(texval, texval, texval, 1.0);
  return bounds * retval;
}

vec4
sampleStack(sampler3D tex, vec4 pos)
{
  vec4 col = sampleAs3DTexture(tex, pos);
  col = luma2Alpha(col, GAMMA_MIN, GAMMA_MAX, GAMMA_SCALE);
  return col;
}

//->intersect AXIS-ALIGNED box routine
//
bool
intersectBox(in vec3 r_o, in vec3 r_d, in vec3 boxMin, in vec3 boxMax, out float tnear, out float tfar)
{
  vec3 invR = vec3(1.0, 1.0, 1.0) / r_d;
  vec3 tbot = invR * (boxMin - r_o);
  vec3 ttop = invR * (boxMax - r_o);
  vec3 tmin = min(ttop, tbot);
  vec3 tmax = max(ttop, tbot);
  float largest_tmin = max(max(tmin.x, tmin.y), max(tmin.x, tmin.z));
  float smallest_tmax = min(min(tmax.x, tmax.y), min(tmax.x, tmax.z));
  tnear = largest_tmin;
  tfar = smallest_tmax;

  // now constrain near and far using clipPlane if active.
  // plane xyz is normal, plane w is -distance from origin.
  float denom = dot(r_d, g_clipPlane.xyz);
  if (abs(denom) > 0.0001f) // if denom is 0 then ray is parallel to plane
  {
    float tClip = dot(g_clipPlane.xyz * (-g_clipPlane.w) - r_o, g_clipPlane.xyz) / denom;
    if (denom < 0.0f) {
      tnear = max(tnear, tClip);
    } else {
      tfar = min(tfar, tClip);
    }
  } else {
    // todo check to see which side of the plane we are on ?
  }

  return (tfar > tnear);
}

vec4
integrateVolume(vec4 eye_o, vec4 eye_d, float tnear, float tfar, float clipNear, float clipFar, sampler3D textureAtlas)
{
  vec4 C = vec4(0.0);
  float tend = min(tfar, clipFar);
  float tbegin = tnear;
  const int maxSteps = 512;
  float csteps = clamp(float(BREAK_STEPS), 1.0, float(maxSteps));
  float invstep = 1.0 / csteps;
  float r = 0.5 - 1.0 * rand(eye_d.xy);
  float tstep = invstep;
  float tfarsurf = r * tstep;
  float overflow = mod((tfarsurf - tend), tstep);
  float t = tbegin + overflow;
  t += r * tstep;
  float tdist = 0.0;
  int numSteps = 0;

  vec4 pos, col;
  float s = 0.5 * float(maxSteps) / csteps;
  for (int i = 0; i < maxSteps; i++) {
    pos = eye_o + eye_d * t;
    pos.xyz = (pos.xyz + 0.5); // 0.5 * (pos + 1.0); // map position from [boxMin, boxMax] to [0, 1] coordinates
    col = sampleStack(textureAtlas, pos);

    // Finish up by adding brightness/density
    col.xyz *= BRIGHTNESS;
    col.w *= DENSITY;
    float stepScale = (1.0 - powf((1.0 - col.w), s));
    col.w = stepScale;
    col.xyz *= col.w;
    col = clamp(col, 0.0, 1.0);

    C = (1.0 - C.w) * col + C;
    t += tstep;
    numSteps = i;
    if (t > tend)
      break;
    if (C.w > 1.0)
      break;
  }
  return C;
}
void
main()
{
  outputColour = vec4(1.0, 0.0, 0.0, 1.0);
  // gl_FragCoord defaults to 0,0 at lower left
  vec2 vUv = gl_FragCoord.xy / iResolution.xy;

  vec3 eyeRay_o, eyeRay_d;
  if (isPerspective != 0.0) {
    // camera position in camera space is 0,0,0!
    eyeRay_o = (inverseModelViewMatrix * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    eyeRay_d = normalize(inData.pObj - eyeRay_o);
  } else {
    float zDist = 2.0;
    eyeRay_d = (inverseModelViewMatrix * vec4(0.0, 0.0, -zDist, 0.0)).xyz;
    vec4 ray_o = vec4(2.0 * vUv - 1.0, 1.0, 1.0);
    ray_o.xy *= orthoScale;
    ray_o.x *= iResolution.x / iResolution.y;
    eyeRay_o = (inverseModelViewMatrix * ray_o).xyz;
  }

  vec3 boxMin = AABB_CLIP_MIN;
  vec3 boxMax = AABB_CLIP_MAX;
  float tnear, tfar;
  bool hit = intersectBox(eyeRay_o, eyeRay_d, boxMin, boxMax, tnear, tfar);
  if (!hit) {
    outputColour = vec4(1.0, 0.0, 1.0, 0.0);
    return;
  }
  // else {
  //		outputColour = vec4(1.0, 1.0, 1.0, 1.0);
  //		return;
  // }
  float clipNear = 0.0;    //-(dot(eyeRay_o.xyz, eyeNorm) + dNear) / dot(eyeRay_d.xyz, eyeNorm);
  float clipFar = 10000.0; //-(dot(eyeRay_o.xyz,-eyeNorm) + dFar ) / dot(eyeRay_d.xyz,-eyeNorm);

  vec4 C = integrateVolume(vec4(eyeRay_o, 1.0), vec4(eyeRay_d, 0.0), tnear, tfar, clipNear, clipFar, textureAtlas);
  C = clamp(C, 0.0, 1.0);
  outputColour = C;
  return;
}
