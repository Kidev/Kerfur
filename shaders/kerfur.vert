#version 440
#define LED_SCREEN_SIZE_FROM_COLOR 0
#define LED_SCREEN_COLOR_FROM_SOURCE 1

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 fragCoord;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec3 iResolution;
    float ledScreenLedSize;
    vec4 ledScreenLedColor;
};

out gl_PerVertex { vec4 gl_Position; };

void main() {
    texCoord = qt_MultiTexCoord0;
    fragCoord = qt_Vertex.xy;
    vec2 vertCoord = qt_Vertex.xy;
    gl_Position = qt_Matrix * vec4(vertCoord, 0.0, 1.0);
}
