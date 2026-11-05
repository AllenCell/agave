// Generated C source file containing shader

#include <string>

const std::string copy_frag_chunk_0 = R"(
#version 400 core

uniform sampler2D tTexture0;
in vec2 vUv;
out vec4 out_FragColor;

void main()
{
  out_FragColor = texture(tTexture0, vUv);
}

)";

const std::string copy_frag_src = 
    copy_frag_chunk_0;
