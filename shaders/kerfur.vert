#version 440

layout(location = 0) in vec4 qt_Vertex;
layout(location = 0) out vec2 fragCoord;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 faceRect;
    vec2 faceCells;
    vec4 ledScreenLedColor;
    float ledScreenLedSize;
    float ledScreenOffLevel;
    float ledScreenPixel;
    float ledScreenFaceOnly;
    float ledScreenStripeGap;
    float ledScreenRowGap;
    float ledScreenWhiteCore;
};

out gl_PerVertex { vec4 gl_Position; };

void main() {
    fragCoord = qt_Vertex.xy;
    gl_Position = qt_Matrix * vec4(qt_Vertex.xy, 0.0, 1.0);
}
