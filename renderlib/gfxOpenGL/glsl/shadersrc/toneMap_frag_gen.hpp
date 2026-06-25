// Generated C source file containing shader

#include <string>

const std::string toneMap_frag_chunk_0 = R"(
#version 400 core

uniform float gInvExposure;
uniform sampler2D tTexture0;
in vec2 vUv;
out vec4 out_FragColor;

vec3 XYZtoRGB(vec3 xyz) {
    return vec3(
        3.240479f*xyz[0] - 1.537150f*xyz[1] - 0.498535f*xyz[2],
        -0.969256f*xyz[0] + 1.875991f*xyz[1] + 0.041556f*xyz[2],
        0.055648f*xyz[0] - 0.204043f*xyz[1] + 1.057311f*xyz[2]
    );
}

void main()
{
    vec4 pixelColor = texture(tTexture0, vUv);

    //out_FragColor = pixelColor;
    //return;

    pixelColor.rgb = XYZtoRGB(pixelColor.rgb);

    pixelColor.rgb = 1.0-exp(-pixelColor.rgb*gInvExposure);
    pixelColor = clamp(pixelColor, 0.0, 1.0);

    out_FragColor = pixelColor;
    //out_FragColor = pow(pixelColor, vec4(1.0/2.2));

return;

/*
    /////////////////////
    /////////////////////
    /////////////////////
    /////////////////////
    //// DENOISING FILTER

    vec4 clr00 = pixelColor;
    vec4 rgbsample = clr00;
    // convert XYZ to RGB here.
    rgbsample.rgb = XYZtoRGB(clr00.rgb);
    // tone map!
    rgbsample.rgb = 1.0 - exp(-rgbsample.rgb * gInvExposure);
    rgbsample = clamp(rgbsample, 0.0, 1.0);
    clr00 = rgbsample;

    float fCount = 0.0;
    float SumWeights = 0.0;
    vec3 clr = vec3(0.0, 0.0, 0.0);

    for (int i = -gDenoiseWindowRadius; i <= gDenoiseWindowRadius; i++) {
      for (int j = -gDenoiseWindowRadius; j <= gDenoiseWindowRadius; j++) {
        // sad face...
        if (Y + i < 0)
          continue;
        if (X + j < 0)
          continue;
        if (Y + i >= gFilmHeight)
          continue;
        if (X + j >= gFilmWidth)
          continue;

        vec4 clrIJ = texture(tTexture0, vUv + vec2(i,j));
        rgbsample.rgb = XYZToRGB(clrIJ.rgb);
        // tone map!
        rgbsample.rgb = 1.0 - exp(-rgbsample.rgb * gInvExposure);
        rgbsample = clamp(rgbsample, 0.0, 1.0);

        clrIJ = rgbsample;

        float distanceIJ = vecLen(clr00, clrIJ);

        // gDenoiseNoise = 1/h^2
        //
        float weightIJ = expf(-(distanceIJ * gDenoiseNoise + (float)(i * i + j * j) * gDenoiseInvWindowArea));

        clr.x += clrIJ.x * weightIJ;
        clr.y += clrIJ.y * weightIJ;
        clr.z += clrIJ.z * weightIJ;

        SumWeights += weightIJ;

        fCount += (weightIJ > gDenoiseWeightThreshold) ? gDenoiseInvWindowArea : 0;
      }
    }

    SumWeights = 1.0f / SumWeights;

    clr.rgb *= SumWeights;

    float LerpQ = (fCount > gDenoiseLerpThreshold) ? gDenoiseLerpC : 1.0f - gDenoiseLerpC;

    clr.rgb = mix(clr.rgb, clr00.rgb, LerpQ);
    clr.rgb = clamp(clr.rgb, 0.0, 1.0);

    out_FragColor = vec4(clr.rgb, clr00.a);
*/
}

)";

const std::string toneMap_frag_src = 
    toneMap_frag_chunk_0;
