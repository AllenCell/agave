// Generated C source file containing shader

#include <string>

const std::string slice_frag_chunk_0 = R"(
#version 400 core

out vec4 out_FragColor;

uniform sampler3D uVolumeTexture;
uniform sampler2DArray uTransferTexture;
uniform vec2 uViewport;
uniform int uMode;
uniform int uChannelCount;
uniform ivec3 uIndices;
uniform ivec3 uDimensions;
uniform vec3 uPhysicalDimensions;
uniform vec3 uFlipAxes;
uniform vec2 uPan;
uniform float uZoom;
// Pane rectangles use bottom-left framebuffer pixels: x, y, width, height.
uniform vec4 uPaneXY;
uniform vec4 uPaneYZ;
uniform vec4 uPaneXZ;

uniform vec4 uIntensityMin;
uniform vec4 uIntensityMax;
uniform vec4 uLutMin;
uniform vec4 uLutMax;
uniform vec4 uLabels;
uniform vec4 uOpacity;
uniform vec4 uDiffuse[4];
uniform ivec4 uTfNodeCounts;
uniform vec4 uTfNodes[64];

const int VIEW_Z = 0;
const int VIEW_Y = 1;
const int VIEW_X = 2;
const int VIEW_TRIPLE = 3;
const float UINT16_MAX_VALUE = 65535.0;

bool
containsPixel(vec4 rect, vec2 pixel)
{
  return rect.z > 0.0 && rect.w > 0.0 && pixel.x >= rect.x && pixel.y >= rect.y &&
         pixel.x < rect.x + rect.z && pixel.y < rect.y + rect.w;
}

vec2
planePhysicalSize(int viewAxis)
{
  if (viewAxis == VIEW_X) {
    return vec2(uPhysicalDimensions.z, uPhysicalDimensions.y);
  }
  if (viewAxis == VIEW_Y) {
    return vec2(uPhysicalDimensions.x, uPhysicalDimensions.z);
  }
  return uPhysicalDimensions.xy;
}

vec4
singleSliceRect(int viewAxis)
{
  vec2 physicalSize = max(planePhysicalSize(viewAxis), vec2(0.000001));
  float fitScale = min(uViewport.x / physicalSize.x, uViewport.y / physicalSize.y);
  vec2 size = physicalSize * fitScale * max(uZoom, 0.0001);
  vec2 origin = 0.5 * (uViewport - size) + uPan;
  return vec4(origin, size);
}

float
indexCoordinate(int index, int dimension)
{
  float d = float(max(dimension, 1));
  return (float(clamp(index, 0, max(dimension, 1) - 1)) + 0.5) / d;
}

float
planeCoordinate(float uv, int dimension)
{
  float d = float(max(dimension, 1));
  return clamp(uv, 0.5 / d, 1.0 - 0.5 / d);
}

float
indexToUv(int index, int dimension)
{
  if (dimension <= 1) {
    return 0.5;
  }
  return float(clamp(index, 0, dimension - 1)) / float(dimension - 1);
}

vec3
samplePosition(int viewAxis, vec2 uv)
{
  float x = planeCoordinate(uv.x, uDimensions.x);
  float y = planeCoordinate(uv.y, uDimensions.y);
  float z = planeCoordinate(uv.y, uDimensions.z);
  if (viewAxis == VIEW_X) {
    return vec3(indexCoordinate(uIndices.x, uDimensions.x),
                y,
                planeCoordinate(uv.x, uDimensions.z));
  }
  if (viewAxis == VIEW_Y) {
    return vec3(x, indexCoordinate(uIndices.y, uDimensions.y), z);
  }
  return vec3(x, y, indexCoordinate(uIndices.z, uDimensions.z));
}

float
evalTransferFunction(int channel, float intensity)
{
  const int MAX_NODES = 16;
  int count = uTfNodeCounts[channel];
  int offset = channel * MAX_NODES;
  if (count <= 0) {
    return 0.0;
  }
  if (intensity <= uTfNodes[offset].x) {
    return uTfNodes[offset].y;
  }
  if (intensity >= uTfNodes[offset + count - 1].x) {
    return uTfNodes[offset + count - 1].y;
  }
  for (int node = 0; node < MAX_NODES - 1; ++node) {
    if (node >= count - 1) {
      break;
    }
    vec2 a = uTfNodes[offset + node].xy;
    vec2 b = uTfNodes[offset + node + 1].xy;
    if (intensity >= a.x && intensity <= b.x) {
      float t = (intensity - a.x) / max(b.x - a.x, 0.000001);
      return mix(a.y, b.y, t);
    }
  }
  return 0.0;
}

vec3
channelColor(float raw, int channel)
{
  vec3 color;
  if (uLabels[channel] > 0.5) {
    int label = int(round(raw)) % 256;
    color = texelFetch(uTransferTexture, ivec3(label, 0, channel), 0).rgb;
  } else {
    float t = clamp((raw - uLutMin[channel]) / max(uLutMax[channel] - uLutMin[channel], 1.0), 0.0, 1.0);
    color = texture(uTransferTexture, vec3(t, 0.5, float(channel))).rgb;
  }
  return color * uDiffuse[channel].rgb;
}

vec4
shadeSlice(vec3 position)
{
  vec3 flipped = vec3(1.0) - position;
  vec3 texturePosition = mix(position, flipped, lessThan(uFlipAxes, vec3(0.0)));
  vec4 raw = texture(uVolumeTexture, texturePosition) * UINT16_MAX_VALUE;
  vec3 weightedColor = vec3(0.0);
  float totalWeight = 0.0;
  for (int channel = 0; channel < 4; ++channel) {
    if (channel >= uChannelCount) {
      break;
    }
    float normalized =
      (raw[channel] - uIntensityMin[channel]) / max(uIntensityMax[channel] - uIntensityMin[channel], 1.0);
    float weight = clamp(evalTransferFunction(channel, normalized) * uOpacity[channel], 0.0, 1.0);
    weightedColor += channelColor(raw[channel], channel) * weight;
    totalWeight += weight;
  }
  if (totalWeight <= 0.000001) {
    return vec4(0.0);
  }
  return vec4(clamp(weightedColor / totalWeight, 0.0, 1.0), clamp(totalWeight, 0.0, 1.0));
}

vec2
crosshairUv(int viewAxis)
{
  if (viewAxis == VIEW_X) {
    return vec2(indexToUv(uIndices.z, uDimensions.z), indexToUv(uIndices.y, uDimensions.y));
  }
  if (viewAxis == VIEW_Y) {
    return vec2(indexToUv(uIndices.x, uDimensions.x), indexToUv(uIndices.z, uDimensions.z));
  }
  return vec2(indexToUv(uIndices.x, uDimensions.x), indexToUv(uIndices.y, uDimensions.y));
}

vec4
drawCrosshair(vec4 color, vec4 rect, int viewAxis, vec2 pixel)
{
  vec2 crosshair = rect.xy + crosshairUv(viewAxis) * rect.zw;
  bool shadow = abs(pixel.x - (crosshair.x + 1.0)) < 0.75 || abs(pixel.y - (crosshair.y - 1.0)) < 0.75;
  bool line = abs(pixel.x - crosshair.x) < 0.75 || abs(pixel.y - crosshair.y) < 0.75;
  if (shadow) {
    color = vec4(0.2, 0.2, 0.2, 1.0);
  }
  if (line) {
    color = vec4(1.0);
  }
  return color;
}

void
main()
{
  vec2 pixel = gl_FragCoord.xy;
  vec4 rect;
  int viewAxis = uMode;
  if (uMode == VIEW_TRIPLE) {
    if (containsPixel(uPaneXY, pixel)) {
      rect = uPaneXY;
      viewAxis = VIEW_Z;
    } else if (containsPixel(uPaneYZ, pixel)) {
      rect = uPaneYZ;
      viewAxis = VIEW_X;
    } else if (containsPixel(uPaneXZ, pixel)) {
      rect = uPaneXZ;
      viewAxis = VIEW_Y;
    } else {
      out_FragColor = vec4(0.0);
      return;
    }
  } else {
    rect = singleSliceRect(viewAxis);
    if (!containsPixel(rect, pixel)) {
      out_FragColor = vec4(0.0);
      return;
    }
  }

  vec2 uv = (pixel - rect.xy) / rect.zw;
  vec4 color = shadeSlice(samplePosition(viewAxis, uv));
  if (uMode == VIEW_TRIPLE) {
    color = drawCrosshair(color, rect, viewAxis, pixel);
  }
  out_FragColor = color;
}

)";

const std::string slice_frag_src =
    slice_frag_chunk_0;
