#version 400 core
in vec4 Frag_color;
in vec2 Frag_UV;
in vec4 gl_FragCoord;
uniform int picking; //< draw for display or for picking? Picking has no texture.
uniform sampler2D Texture;
out vec4 outputF;

const float EPSILON = 0.1;

void
main()
{
  vec4 result = Frag_color;

  // When drawing selection codes, everything is opaque.
  if (picking == 1) {
    result.w = 1.0;
  }

  // Gesture geometry handshake: any uv value below -64 means
  // no texture lookup. Check VertsCode::k_noTexture
  // (add an epsilon to fix some fp errors.
  // TODO check to see if highp would have helped)
  if (picking == 0 && Frag_UV.x > -64 + EPSILON) {
    result *= texture(Texture, Frag_UV.xy);
  }

  // Gesture geometry handshake: any uv equal to -128 means
  // overlay a checkerboard pattern. Check VertsCode::k_marqueePattern
  if (Frag_UV.s == -128.0) {
    // Create a pixel checkerboard pattern used for marquee
    // selection
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    if (((x + y) & 1) == 0)
      result = vec4(0, 0, 0, 1);
  }
  outputF = result;
}
