#version 440

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec3 iResolution;
    float ledScreenLedSize;
    vec4 ledScreenLedColor;
};

layout(binding = 1) uniform sampler2D iSource;
layout(binding = 2) uniform sampler2D scaledSourceImage;

void main() {
    fragColor = texture(iSource, texCoord);
    {
        const float maxLedSize = ledScreenLedSize;
        vec2 center = floor(fragCoord / maxLedSize) * maxLedSize + maxLedSize * 0.5;
        vec3 ledColor = texture(scaledSourceImage, center / iResolution.xy).rgb;
        float ledSize = maxLedSize;

        float alpha = 0.0;
        if (ledSize > 2.0) {
            float dist = distance(center, fragCoord) * 2.0;
            alpha = smoothstep(1.0, 0.5, dist / ledSize);
        }

        fragColor.rgb = ledColor.rgb * alpha;

    }
    fragColor = fragColor * qt_Opacity;
}
