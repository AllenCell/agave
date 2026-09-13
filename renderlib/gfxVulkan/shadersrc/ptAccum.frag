#version 450

layout(location = 0) in VertexData
{
  vec2 pObj;
} inData;

layout(location = 0) out vec4 outputColour;

layout(set = 0, binding = 0, std140) uniform PtAccumFragParams
{
  mat4 inverseModelViewMatrix;
  int numIterations;
}
ptAccumFragParams;

layout(set = 0, binding = 1) uniform sampler2D textureRender;
layout(set = 0, binding = 2) uniform sampler2D textureAccum;

#define numIterations ptAccumFragParams.numIterations

vec4 CumulativeMovingAverage(vec4 A, vec4 Ax, int N)
{
	 return A + ((Ax - A) / max(float(N), 1.0f));
}

void main()
{
    vec4 accum = textureLod(textureAccum, (inData.pObj+1.0) / 2.0, 0).rgba;
    vec4 render = textureLod(textureRender, (inData.pObj + 1.0) / 2.0, 0).rgba;

	//outputColour = accum + render;
	outputColour = CumulativeMovingAverage(accum, render, numIterations);
	return;
}
