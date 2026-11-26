#version 440
// The face as an LED panel seen up close. Every cell is one pixel of the face
// picture, split across into three stripes a third of the cell wide and the
// whole cell high: blue, green and red, left to right. Each stripe is lit by
// its own channel of the face colour, so the colour is something the eye
// mixes rather than something any one stripe has. A thin dark line separates
// two stripes, and another separates two rows of cells. Cells cover the whole
// item, unlit outside the picture; the strip narrower than a cell left at
// each edge stays dark.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // This item's width and height.
    vec2 itemSize;
    // The face picture, as x, y, width, height in this item's coordinates.
    vec4 faceRect;
    // Cells across and down the picture: the size of cellLevels.
    vec2 faceCells;
    // The cells drawn on the item, as the first column and row and the number
    // of each, counted in cells from the picture's top left cell.
    vec4 ledCells;
    // What a fully lit cell adds up to.
    vec4 ledScreenLedColor;
    // Cell pitch, in item pixels.
    float ledScreenLedSize;
    // How bright a stripe that is off still looks.
    float ledScreenOffLevel;
    // One device pixel, in item pixels: the width the panel is filtered over.
    float ledScreenPixel;
    // Width of the dark line between two stripes, as a fraction of the cell.
    float ledScreenStripeGap;
    // Height of the dark line between two rows, as a fraction of the cell.
    float ledScreenRowGap;
    // How far a bright stripe washes towards white at its core.
    float ledScreenWhiteCore;
};

// One texel per cell: how lit the cell is, in the red channel.
layout(binding = 1) uniform sampler2D cellLevels;

// 1 for a cell from `first` up to, but not including, `end`, 0 otherwise.
float inCells(vec2 cell, vec2 first, vec2 end)
{
    vec2 inside = step(first, cell) * (vec2(1.0) - step(end, cell));
    return inside.x * inside.y;
}

// 1 for a cell of the picture, 0 for one outside it.
float inPicture(vec2 cell)
{
    return inCells(cell, vec2(0.0), faceCells);
}

// The light a cell of the picture puts out. The texture is read for every
// cell, clamped to the picture, so no read sits behind a branch that differs
// between neighbouring pixels.
vec3 cellEmission(vec2 cell)
{
    vec2 texel = clamp(cell, vec2(0.0), faceCells - 1.0);
    float level = texture(cellLevels, (texel + 0.5) / faceCells).r;
    return inPicture(cell) * level * ledScreenLedColor.rgb;
}

void main()
{
    // A cell is never narrower than the filter, so the footprint of one
    // device pixel touches at most two cells on each axis.
    float pitch = max(ledScreenLedSize, ledScreenPixel);
    float footprint = ledScreenPixel / pitch;

    // Where each stripe is lit across a cell, from 0 to 1, in the order of the
    // stripes (blue, green, red), and where a row is lit down it. Half of each
    // line falls on either side of the boundary it sits on.
    float stripeGap = clamp(ledScreenStripeGap, 0.0, 1.0 / 3.0);
    float rowGap = clamp(ledScreenRowGap, 0.0, 1.0);
    vec3 stripeStart = vec3(0.0, 1.0, 2.0) / 3.0 + 0.5 * stripeGap;
    vec3 stripeEnd = vec3(1.0, 2.0, 3.0) / 3.0 - 0.5 * stripeGap;
    float rowStart = 0.5 * rowGap;
    float rowEnd = 1.0 - 0.5 * rowGap;

    // The grid starts at the picture's corner, so each cell centre lands on a
    // texel centre of the per cell image.
    vec2 position = (qt_TexCoord0 * itemSize - faceRect.xy) / pitch;
    vec2 low = position - 0.5 * footprint;
    vec2 high = position + 0.5 * footprint;
    vec2 first = floor(low);

    // The device pixel is a box over [low, high]. Each stripe's share of it is
    // measured exactly, so a stripe or a line narrower than a pixel blends
    // into its neighbours instead of beating against the pixel grid.
    vec3 color = vec3(0.0);
    for (int j = 0; j < 2; ++j) {
        float row = first.y + float(j);
        float down = (clamp(high.y - row, rowStart, rowEnd) - clamp(low.y - row, rowStart, rowEnd)) / footprint;
        for (int i = 0; i < 2; ++i) {
            vec2 cell = vec2(first.x + float(i), row);
            float drawn = inCells(cell, ledCells.xy, ledCells.xy + ledCells.zw);
            vec3 stripes = (clamp(vec3(high.x - cell.x), stripeStart, stripeEnd)
                            - clamp(vec3(low.x - cell.x), stripeStart, stripeEnd)) / footprint;
            // Reordered from blue, green, red to the channels' order.
            vec3 share = stripes.zyx * (down * drawn);
            vec3 emitted = cellEmission(cell);
            color += share * emitted
                     + vec3(dot(share, vec3(ledScreenOffLevel) + ledScreenWhiteCore * emitted * emitted));
        }
    }

    color = min(color, vec3(1.0));
    fragColor = vec4(color, max(color.r, max(color.g, color.b))) * qt_Opacity;
}
