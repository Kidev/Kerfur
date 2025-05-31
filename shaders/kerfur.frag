#version 440
// 0 = diamond, 1 = square, 2+ = circle
#define SHAPE_TYPE 1

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec3 iResolution;
    float ledScreenLedSize;
    vec4 ledScreenLedColor;
    float ledScreenGridStep;
};

layout(binding = 1) uniform sampler2D iSource;
layout(binding = 2) uniform sampler2D scaledSourceImage;

float calculateAlpha(vec2 center, vec2 fragCoord, float ledSize) {
    float alpha = 0.0;

    if (ledSize <= 2.0) return 0.0;

#if SHAPE_TYPE == 0
    vec2 delta = abs(center - fragCoord) * 2.0;
    float manhattanDist = delta.x + delta.y;
    alpha = smoothstep(1.0, 0.7, manhattanDist / ledSize);
#elif SHAPE_TYPE == 1
    vec2 delta = abs(center - fragCoord) * 2.0;
    float maxDelta = max(delta.x, delta.y);
    alpha = smoothstep(1.0, 0.8, maxDelta / ledSize);
#else
    float dist = distance(center, fragCoord) * 2.0;
    alpha = smoothstep(1.0, 0.5, dist / ledSize);
#endif
    return alpha;
}

void main() {
    fragColor = texture(iSource, texCoord);
    {
        const float maxLedSize = ledScreenLedSize;
        // Use grid step for positioning if provided, otherwise use led size
        float gridStep = (ledScreenGridStep > 0.0) ? ledScreenGridStep : maxLedSize;

        vec2 center = floor(fragCoord / gridStep) * gridStep + gridStep * 0.5;
        vec3 ledColor = texture(scaledSourceImage, center / iResolution.xy).rgb;
        float ledSize = maxLedSize;

        float alpha = calculateAlpha(center, fragCoord, ledSize);

        fragColor.rgb = ledColor.rgb * alpha;
    }
    fragColor = fragColor * qt_Opacity;
}
