#version 440
#define BLUR_HELPER_MAX_LEVEL 64

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float blurMultiplier;
    int glowBlendMode;
    float glowBlurAmount;
    float glowBloom;
    float glowMaxBrightness;
    vec4 glowColor;
};

layout(binding = 1) uniform sampler2D iSource;

layout(binding = 2) uniform sampler2D iSourceBlur1;
layout(binding = 3) uniform sampler2D iSourceBlur2;
layout(binding = 4) uniform sampler2D iSourceBlur3;
layout(binding = 5) uniform sampler2D iSourceBlur4;
layout(binding = 6) uniform sampler2D iSourceBlur5;

layout(location = 2) in vec4 blurWeight1;
layout(location = 3) in vec2 blurWeight2;

void main() {
    fragColor = texture(iSource, texCoord);
    {
        vec4 glow = texture(iSource, texCoord) * blurWeight1[0];
        glow += texture(iSourceBlur1, texCoord) * blurWeight1[1];
    #if (BLUR_HELPER_MAX_LEVEL > 2)
        glow += texture(iSourceBlur2, texCoord) * blurWeight1[2];
    #endif
    #if (BLUR_HELPER_MAX_LEVEL > 8)
        glow += texture(iSourceBlur3, texCoord) * blurWeight1[3];
    #endif
    #if (BLUR_HELPER_MAX_LEVEL > 16)
        glow += texture(iSourceBlur4, texCoord) * blurWeight2[0];
    #endif
    #if (BLUR_HELPER_MAX_LEVEL > 32)
        glow += texture(iSourceBlur5, texCoord) * blurWeight2[1];
    #endif

        glow = min(glow * glowBloom, vec4(glowMaxBrightness));
        glow = mix(glow, glow.a * glowColor, glowColor.a);

        // Blend in the Glow
        if (glowBlendMode == 0) {
            // Additive
            fragColor += glow;
        } else if (glowBlendMode == 1) {
            // Screen
            fragColor = clamp(fragColor, vec4(0.0), vec4(1.0));
            fragColor = max((fragColor + glow) - (fragColor * glow), vec4(0.0));
        } else if (glowBlendMode == 2) {
            // Replace
            fragColor = glow;
        } else {
            // Outer
            fragColor = mix(glow, fragColor, fragColor.a);
        }
    }
    fragColor = fragColor * qt_Opacity;
}
