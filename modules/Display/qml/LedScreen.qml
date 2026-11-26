import QtQuick
import Display
import qml 1.0

/// The face drawn as an LED panel: the screen fills this item, and `source` is
/// lit on it at `pictureRect`, which the caller lays `source` over.
///
/// With `ledScreenLedSize` at 0 (automatic) every pixel of the picture is one
/// cell: the cell is the largest whole number of device pixels that fits
/// `sourceSize` cells on the screen, and the picture is centred. With a size in
/// pixels, the picture fills the item with its aspect ratio kept. Either way,
/// unlit cells repeat from the picture out to the edges of the item, and the
/// strip narrower than a cell left at each edge stays dark.
///
/// A cell that the picture lights comes on over `ledScreenFadeIn` and one it
/// leaves goes dark over `ledScreenFadeOut`. The panel redraws on its own only
/// while a cell is still on its way.
Item {
    id: root

    readonly property bool automaticLedSize: root.ledScreenLedSize <= 0
    property real blurMultiplier: KSettings.blurMultiplier
    /// Cells across and down the picture.
    readonly property size cells: root.automaticLedSize ? root.pictureCells : Qt.size(Math.max(1, Math.round(root.pictureRect.width / root.pitch)), Math.max(1, Math.round(root.pictureRect.height / root.pitch)))
    /// One device pixel, in item pixels.
    readonly property real devicePixel: 1 / Math.max(1, Screen.devicePixelRatio)
    property int glowBlendMode: KSettings.glowBlendMode
    property real glowBloom: KSettings.glowBloom
    property real glowBlurAmount: KSettings.glowBlurAmount
    property color glowColor: KSettings.glowColor
    property real glowMaxBrightness: KSettings.glowMaxBrightness
    /// The cells drawn on the item: the first column and row, and the number
    /// of each, counted in cells from the picture's top left cell. Only whole
    /// cells are drawn.
    readonly property rect ledCells: {
        // Absorbs rounding, so a cell that ends exactly on an edge is kept.
        const slack = 0.001;
        const left = Math.floor(root.pictureRect.x / root.pitch + slack);
        const top = Math.floor(root.pictureRect.y / root.pitch + slack);
        const right = Math.floor((root.width - root.pictureRect.x) / root.pitch + slack);
        const bottom = Math.floor((root.height - root.pictureRect.y) / root.pitch + slack);
        return Qt.rect(-left, -top, Math.max(0, left + right), Math.max(0, top + bottom));
    }
    /// Milliseconds a cell takes to come on fully from dark; 0 is at once.
    property int ledScreenFadeIn: KSettings.ledScreenFadeIn
    /// Milliseconds a cell takes to go dark from fully lit; 0 is at once.
    property int ledScreenFadeOut: KSettings.ledScreenFadeOut
    property color ledScreenLedColor: KSettings.glowColor
    /// Cell pitch in item pixels, or 0 for one cell per picture pixel.
    property real ledScreenLedSize: KSettings.ledScreenLedSize
    /// How bright a stripe that is off still looks, from 0 to 1.
    property real ledScreenOffLevel: KSettings.ledScreenOffLevel
    /// Height of the dark line between two rows, as a fraction of the cell.
    property real ledScreenRowGap: KSettings.ledScreenRowGap
    /// Width of the dark line between two stripes, as a fraction of the cell.
    property real ledScreenStripeGap: KSettings.ledScreenStripeGap
    /// How far a bright stripe washes towards white at its core, from 0 to 1.
    property real ledScreenWhiteCore: KSettings.ledScreenWhiteCore
    /// One cell, in pixels of the picture `source` shows. A drawing that
    /// moves by whole multiples of it lights the same shape at every step.
    readonly property size pictureCell: Qt.size(root.pictureCells.width / root.cells.width, root.pictureCells.height / root.cells.height)
    readonly property size pictureCells: Qt.size(Math.max(1, Math.round(root.sourceSize.width)), Math.max(1, Math.round(root.sourceSize.height)))
    /// Where the picture sits, in this item's coordinates.
    readonly property rect pictureRect: {
        const pictureWidth = root.automaticLedSize ? root.pitch * root.pictureCells.width : Math.min(root.width, root.height * root.pictureCells.width / root.pictureCells.height);
        const pictureHeight = root.automaticLedSize ? root.pitch * root.pictureCells.height : Math.min(root.height, root.width * root.pictureCells.height / root.pictureCells.width);
        // Whole device pixels, so every cell edge lands on the same place
        // within a pixel and all cells look alike.
        const x = Math.floor((root.width - pictureWidth) / 2 / root.devicePixel) * root.devicePixel;
        const y = Math.floor((root.height - pictureHeight) / 2 / root.devicePixel) * root.devicePixel;
        return Qt.rect(x, y, pictureWidth, pictureHeight);
    }
    /// Cell pitch in item pixels.
    readonly property real pitch: root.automaticLedSize ? Math.max(1, Math.floor(Math.min(root.width / root.pictureCells.width, root.height / root.pictureCells.height) / root.devicePixel)) * root.devicePixel : Math.max(root.devicePixel, root.ledScreenLedSize)
    property Item source: null
    /// Pixels across and down the picture `source` shows.
    property size sourceSize: Qt.size(1, 1)

    Component.onCompleted: fadeClock.wake()
    onCellsChanged: fadeClock.wake()

    // The picture, one texel per cell.
    ShaderEffectSource {
        id: scaledSource

        hideSource: true
        smooth: false
        sourceItem: root.source
        textureSize: root.cells
    }

    // One step of every cell towards the picture, drawn into `shownLevelsSource`.
    ShaderEffect {
        id: fadeStepEffect

        readonly property real fadeIn: root.ledScreenFadeIn / 1000
        readonly property real fadeOut: root.ledScreenFadeOut / 1000
        property real fadeStep: 0
        readonly property var shownLevels: shownLevelsSource
        readonly property var targetLevels: scaledSource

        blending: false
        fragmentShader: '/shaders/ledfade.frag.qsb'
        height: root.cells.height
        width: root.cells.width
    }

    // How lit each cell is, kept from one step to the next. It is redrawn only
    // when `fadeClock` asks: a live source that reads itself redraws, and so
    // redraws the whole screen, on every frame.
    ShaderEffectSource {
        id: shownLevelsSource

        hideSource: true
        live: false
        recursive: true
        smooth: false
        sourceItem: fadeStepEffect
        textureSize: root.cells
    }

    TextureWatch {
        source: scaledSource

        onTextureChanged: fadeClock.wake()
    }

    // Steps the cells once a frame, from a change of the picture until the
    // slower of the two fades has had time to finish.
    FrameAnimation {
        id: fadeClock

        // A step after a stall covers at most this many seconds, so a frame
        // that comes late cannot cut a fade short.
        readonly property real longestStep: 0.05
        // Seconds left before every cell has reached the picture.
        property real remaining: 0

        function wake() {
            fadeClock.remaining = Math.max(root.ledScreenFadeIn, root.ledScreenFadeOut) / 1000;
            fadeClock.start();
        }

        onTriggered: {
            const step = Math.min(fadeClock.frameTime, fadeClock.longestStep);
            fadeStepEffect.fadeStep = step;
            shownLevelsSource.scheduleUpdate();
            fadeClock.remaining -= step;
            if (fadeClock.remaining < 0) {
                fadeClock.stop();
            }
        }
    }

    ShaderEffect {
        id: ledScreenEffect

        readonly property var cellLevels: shownLevelsSource
        readonly property size faceCells: root.cells
        readonly property rect faceRect: root.pictureRect
        readonly property size itemSize: Qt.size(ledScreenEffect.width, ledScreenEffect.height)
        readonly property rect ledCells: root.ledCells
        readonly property color ledScreenLedColor: root.ledScreenLedColor
        readonly property real ledScreenLedSize: root.pitch
        readonly property real ledScreenOffLevel: root.ledScreenOffLevel
        readonly property real ledScreenPixel: root.devicePixel
        readonly property real ledScreenRowGap: root.ledScreenRowGap
        readonly property real ledScreenStripeGap: root.ledScreenStripeGap
        readonly property real ledScreenWhiteCore: root.ledScreenWhiteCore

        anchors.fill: parent
        blending: true
        fragmentShader: '/shaders/kerfur.frag.qsb'
        layer.enabled: true
        layer.smooth: true
    }

    Glow {
        anchors.fill: parent
        blurMultiplier: root.blurMultiplier
        glowBlendMode: root.glowBlendMode
        glowBloom: root.glowBloom
        glowBlurAmount: root.glowBlurAmount
        glowColor: root.glowColor
        glowMaxBrightness: root.glowMaxBrightness
        source: ledScreenEffect
    }
}
