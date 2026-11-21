#version 400 core

in VertexData
{
  vec2 pObj;
} inData;

out vec4 outputColour;

uniform mat4 inverseModelViewMatrix;

uniform sampler2D textureRender;
uniform sampler2D textureAccum;

uniform int numIterations;

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
