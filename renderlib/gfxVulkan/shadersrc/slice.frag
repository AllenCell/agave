#version 450

layout(location = 0) out vec4 outputColor;

layout(set = 0, binding = 0, std140) uniform SliceParams
{
  vec4 viewportModeChannels;
  ivec4 indices;
  ivec4 dimensions;
  vec4 panZoomGap;
  vec4 physicalDimensions;
  vec4 flipAxes;
  vec4 paneXY;
  vec4 paneYZ;
  vec4 paneXZ;
  vec4 intensityMin;
  vec4 intensityMax;
  vec4 lutMin;
  vec4 lutMax;
  vec4 labels;
  vec4 opacity;
  vec4 diffuse[4];
  ivec4 tfNodeCounts;
  vec4 tfNodes[64];
}
u;

layout(set = 0, binding = 1) uniform sampler3D volumeTexture;
layout(set = 0, binding = 2) uniform sampler2DArray transferTexture;

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
    return vec2(u.physicalDimensions.z, u.physicalDimensions.y);
  }
  if (viewAxis == VIEW_Y) {
    return vec2(u.physicalDimensions.x, u.physicalDimensions.z);
  }
  return u.physicalDimensions.xy;
}

vec4
singleSliceRect(int viewAxis)
{
  vec2 viewport = u.viewportModeChannels.xy;
  vec2 physicalSize = max(planePhysicalSize(viewAxis), vec2(0.000001));
  float fitScale = min(viewport.x / physicalSize.x, viewport.y / physicalSize.y);
  vec2 size = physicalSize * fitScale * max(u.panZoomGap.z, 0.0001);
  vec2 origin = 0.5 * (viewport - size) + u.panZoomGap.xy;
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

vec3
samplePosition(int viewAxis, vec2 uv)
{
  float x = planeCoordinate(uv.x, u.dimensions.x);
  float y = planeCoordinate(uv.y, u.dimensions.y);
  float z = planeCoordinate(uv.y, u.dimensions.z);
  if (viewAxis == VIEW_X) {
    return vec3(indexCoordinate(u.indices.x, u.dimensions.x),
                y,
                planeCoordinate(uv.x, u.dimensions.z));
  }
  if (viewAxis == VIEW_Y) {
    return vec3(x, indexCoordinate(u.indices.y, u.dimensions.y), z);
  }
  return vec3(x, y, indexCoordinate(u.indices.z, u.dimensions.z));
}

float
evalTransferFunction(int channel, float intensity)
{
  const int MAX_NODES = 16;
  int count = u.tfNodeCounts[channel];
  int offset = channel * MAX_NODES;
  if (count <= 0) {
    return 0.0;
  }
  if (intensity <= u.tfNodes[offset].x) {
    return u.tfNodes[offset].y;
  }
  if (intensity >= u.tfNodes[offset + count - 1].x) {
    return u.tfNodes[offset + count - 1].y;
  }
  for (int node = 0; node < MAX_NODES - 1; ++node) {
    if (node >= count - 1) {
      break;
    }
    vec2 a = u.tfNodes[offset + node].xy;
    vec2 b = u.tfNodes[offset + node + 1].xy;
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
  if (u.labels[channel] > 0.5) {
    int label = int(round(raw)) % 256;
    color = texelFetch(transferTexture, ivec3(label, 0, channel), 0).rgb;
  } else {
    float t = clamp((raw - u.lutMin[channel]) / max(u.lutMax[channel] - u.lutMin[channel], 1.0), 0.0, 1.0);
    color = texture(transferTexture, vec3(t, 0.5, float(channel))).rgb;
  }
  return color * u.diffuse[channel].rgb;
}

vec4
shadeSlice(vec3 position)
{
  vec3 flipped = vec3(1.0) - position;
  vec3 texturePosition = mix(position, flipped, lessThan(u.flipAxes.xyz, vec3(0.0)));
  vec4 raw = texture(volumeTexture, texturePosition) * UINT16_MAX_VALUE;
  vec3 weightedColor = vec3(0.0);
  float totalWeight = 0.0;
  int channelCount = int(u.viewportModeChannels.w + 0.5);
  for (int channel = 0; channel < 4; ++channel) {
    if (channel >= channelCount) {
      break;
    }
    float normalized =
      (raw[channel] - u.intensityMin[channel]) / max(u.intensityMax[channel] - u.intensityMin[channel], 1.0);
    float weight = clamp(evalTransferFunction(channel, normalized) * u.opacity[channel], 0.0, 1.0);
    weightedColor += channelColor(raw[channel], channel) * weight;
    totalWeight += weight;
  }
  if (totalWeight <= 0.000001) {
    return vec4(0.0);
  }
  return vec4(clamp(weightedColor / totalWeight, 0.0, 1.0), clamp(totalWeight, 0.0, 1.0));
}

void
main()
{
  // Vulkan fragment coordinates have a top-left origin. Convert once so all
  // layout and pan math matches the OpenGL shader's bottom-left pixel space.
  vec2 viewport = u.viewportModeChannels.xy;
  vec2 pixel = vec2(gl_FragCoord.x, viewport.y - gl_FragCoord.y);
  int mode = int(u.viewportModeChannels.z + 0.5);
  vec4 rect;
  int viewAxis = mode;
  if (mode == VIEW_TRIPLE) {
    if (containsPixel(u.paneXY, pixel)) {
      rect = u.paneXY;
      viewAxis = VIEW_Z;
    } else if (containsPixel(u.paneYZ, pixel)) {
      rect = u.paneYZ;
      viewAxis = VIEW_X;
    } else if (containsPixel(u.paneXZ, pixel)) {
      rect = u.paneXZ;
      viewAxis = VIEW_Y;
    } else {
      outputColor = vec4(0.0);
      return;
    }
  } else {
    rect = singleSliceRect(viewAxis);
    if (!containsPixel(rect, pixel)) {
      outputColor = vec4(0.0);
      return;
    }
  }

  vec2 uv = (pixel - rect.xy) / rect.zw;
  outputColor = shadeSlice(samplePosition(viewAxis, uv));
}
