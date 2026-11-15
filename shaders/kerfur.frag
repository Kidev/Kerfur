#version 440
// The face as an LED panel seen up close: every cell is a triad of red, green
// and blue stripes, each lit by its own channel of the face colour, so the
// colour is something the eye mixes rather than something any one stripe has.

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // The face picture, as x, y, width, height in this item's coordinates.
    vec4 faceRect;
    // Cells across and down the picture: the size of scaledSourceImage.
    vec2 faceCells;
    // What a fully lit cell adds up to.
    vec4 ledScreenLedColor;
    // Cell pitch, in item pixels.
    float ledScreenLedSize;
    // How bright a stripe that is off still looks.
    float ledScreenOffLevel;
};

// One texel per cell, the cell's colour.
layout(binding = 1) uniform sampler2D scaledSourceImage;

// Fraction of its third of the cell a stripe fills across, and of the cell a
// row of stripes fills down.
const float stripeFill = 0.70;
const float rowFill = 0.84;
// How far a bright stripe washes towards white at its core.
const float whiteCore = 0.22;
// Below this pitch a stripe would be under two pixels wide and alias into
// moire, so the cell is drawn as one block instead.
const float minimumTriadPitch = 6.0;

// 1 inside the centred band covering `fill` of the unit interval, 0 outside,
// with an edge `soft` wide on either side.
float band(float x, float fill, float soft)
{
    float edge = abs(x - 0.5) * 2.0;
    return 1.0 - smoothstep(fill - soft, fill + soft, edge);
}

void main()
{
    float pitch = max(ledScreenLedSize, 1.0);

    // The grid starts at the picture's corner, so each cell centre lands on a
    // texel centre of the per cell image.
    vec2 cellPosition = (fragCoord - faceRect.xy) / pitch;
    vec2 cell = floor(cellPosition);
    vec2 local = cellPosition - cell;

    vec3 emitted = vec3(0.0);
    if (all(greaterThanEqual(cell, vec2(0.0))) && all(lessThan(cell, faceCells))) {
        // The picture is white on transparent, premultiplied: its brightest
        // channel is how much of the cell is face.
        vec4 face = texture(scaledSourceImage, (cell + 0.5) / faceCells);
        emitted = max(face.r, max(face.g, face.b)) * ledScreenLedColor.rgb;
    }

    vec3 color;
    if (pitch < minimumTriadPitch) {
        color = (emitted + vec3(ledScreenOffLevel))
                * band(local.x, 0.84, 1.0 / pitch) * band(local.y, 0.84, 1.0 / pitch);
    } else {
        float third = local.x * 3.0;
        float index = floor(third);
        vec3 primary = vec3(equal(vec3(index), vec3(0.0, 1.0, 2.0)));
        float value = dot(emitted, primary);

        // Edges one item pixel wide: a stripe is pitch / 3 pixels across, a
        // row pitch pixels down.
        float mask = band(third - index, stripeFill, 3.0 / pitch) * band(local.y, rowFill, 1.0 / pitch);
        color = (primary * value + vec3(ledScreenOffLevel + whiteCore * value * value)) * mask;
    }

    color = min(color, vec3(1.0));
    fragColor = vec4(color, max(color.r, max(color.g, color.b))) * qt_Opacity;
}
