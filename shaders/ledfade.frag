#version 440
// One step of the LED panel's persistence, one texel per cell. A cell darker
// than the picture asks rises towards it, and one brighter falls, each at the
// rate of a full swing between black and fully lit in fadeIn or fadeOut.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // Seconds since the previous step.
    float fadeStep;
    // Seconds a full rise and a full fall take; 0 is at once.
    float fadeIn;
    float fadeOut;
};

// The picture, one texel per cell.
layout(binding = 1) uniform sampler2D targetLevels;
// The levels the panel showed after the previous step.
layout(binding = 2) uniform sampler2D shownLevels;

// The levels are stored in eight bits, so a step smaller than one of them
// would round back to where it started and the cell would never get there.
const float smallestStep = 1.0 / 255.0;

void main()
{
    // The picture is white on transparent, premultiplied: its brightest
    // channel is how much of the cell is face.
    vec4 face = texture(targetLevels, qt_TexCoord0);
    float target = max(face.r, max(face.g, face.b));
    float shown = texture(shownLevels, qt_TexCoord0).r;

    float rise = fadeIn > 0.0 ? max(fadeStep / fadeIn, smallestStep) : 1.0;
    float fall = fadeOut > 0.0 ? max(fadeStep / fadeOut, smallestStep) : 1.0;
    float level = target > shown ? min(target, shown + rise) : max(target, shown - fall);
    fragColor = vec4(level);
}
