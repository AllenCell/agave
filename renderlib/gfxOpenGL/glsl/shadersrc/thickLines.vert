#version 400 core

// this is the layout of the stripVerts buffer below
// layout (location = 0) in vec3 vPos;
// layout (location = 1) in vec2 vUV;
// layout (location = 2) in vec4 vCol;
// layout (location = 3) in uint vCode;

uniform mat4 projection;
uniform vec2 resolution;
uniform int stripVertexOffset;
uniform int picking;
uniform float thickness;

// this will be defined with R32f format so we can read it one float at a time
uniform samplerBuffer stripVerts;

out vec4 Frag_color;
out vec2 Frag_UV;

void
main()
{
  int line_i = (gl_VertexID) / 6;
  int tri_i = (gl_VertexID) % 6;

  uint vCode = 0;
  vec2 vUV = vec2(0.0, 0.0);
  vec4 vCol = vec4(0.0, 0.0, 0.0, 0.0);

  vec4 va[4];
  // put everything in pixel space so we can apply thickness and
  // compute miters
  for (int i = 0; i < 4; ++i) {
    vec4 vtx;
    vtx.x = texelFetch(stripVerts, (stripVertexOffset + line_i + i) * 10 + 0).x;
    vtx.y = texelFetch(stripVerts, (stripVertexOffset + line_i + i) * 10 + 1).x;
    vtx.z = texelFetch(stripVerts, (stripVertexOffset + line_i + i) * 10 + 2).x;
    vtx.w = 1.0;
    va[i] = projection * vtx;
    va[i].xyz /= va[i].w;
    va[i].xy = (va[i].xy + 1.0) * 0.5 * resolution;
  }

  // it is possible for the projected line points to be very close to each other
  // and so nearly coincident that the length is approaching zero.
  vec2 v_line = va[2].xy - va[1].xy;
  float len = length(v_line);
  if (len < 1.0) {
    // if the line is too short, position the vertex so it will be clipped, out of view frustum
    gl_Position = vec4(1, 1, 1, 0);
    Frag_UV = vec2(0.0, 0.0);
    Frag_color = vec4(1.0, 1.0, 1.0, 1.0);
    return;
  }
  v_line = normalize(v_line);
  // rotate x,y to perpendicular to the line: (y, -x)
  vec2 nv_line = vec2(-v_line.y, v_line.x);

  vec4 pos;
  if (tri_i == 0 || tri_i == 1 || tri_i == 3) {
    vec2 vtan = va[1].xy - va[0].xy;
    vec2 v_pred = length(vtan) > 0.5 ? normalize(vtan) : vtan;
    vec2 v_miter = normalize(nv_line + vec2(-v_pred.y, v_pred.x));

    pos = va[1];
    float d = dot(v_miter, nv_line);
    d = abs(d) < 1 ? 1.0 : d;
    pos.xy += v_miter * thickness * (tri_i == 1 ? -0.5 : 0.5) / d;

    vUV.x = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 3).x;
    vUV.y = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 4).x;
    vCol.x = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 5).x;
    vCol.y = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 6).x;
    vCol.z = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 7).x;
    vCol.w = texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 8).x;
    vCode = floatBitsToUint(texelFetch(stripVerts, (stripVertexOffset + line_i + 1) * 10 + 9).x);
  } else {
    vec2 vtan = va[3].xy - va[2].xy;
    vec2 v_succ = length(vtan) > 0.5 ? normalize(vtan) : vtan;
    vec2 v_miter = normalize(nv_line + vec2(-v_succ.y, v_succ.x));

    pos = va[2];
    float d = dot(v_miter, nv_line);
    d = abs(d) < 1 ? 1.0 : d;
    pos.xy += v_miter * thickness * (tri_i == 5 ? 0.5 : -0.5) / d;

    vUV.x = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 3).x;
    vUV.y = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 4).x;

    vCol.x = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 5).x;
    vCol.y = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 6).x;
    vCol.z = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 7).x;
    vCol.w = texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 8).x;

    vCode = floatBitsToUint(texelFetch(stripVerts, (stripVertexOffset + line_i + 2) * 10 + 9).x);
  }

  // undo the perspective divide and go back to clip space
  pos.xy = pos.xy / resolution * 2.0 - 1.0;
  pos.xyz *= pos.w;
  gl_Position = pos;

  Frag_UV = vUV;
  if (picking == 1) {
    Frag_color = vec4(
      float(vCode & 0xffu) / 255.0, float((vCode >> 8) & 0xffu) / 255.0, float((vCode >> 16) & 0xffu) / 255.0, 1.0);
  } else {
    Frag_color = vCol;
  }
}
