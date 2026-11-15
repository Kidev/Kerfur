import QtQuick
import Tools
import qml 1.0

/// The face drawn as an LED panel: the screen fills this item, and `source` is
/// lit on it at `sourceRect`. Cells outside the picture are still drawn, unlit.
Item {
    id: root

    property real blurMultiplier: KSettings.blurMultiplier
    property int glowBlendMode: KSettings.glowBlendMode
    property real glowBloom: KSettings.glowBloom
    property real glowBlurAmount: KSettings.glowBlurAmount
    property color glowColor: KSettings.glowColor
    property real glowMaxBrightness: KSettings.glowMaxBrightness
    property color ledScreenLedColor: KSettings.glowColor
    property real ledScreenLedSize: KSettings.ledScreenLedSize
    /// How bright a stripe that is off still looks, from 0 to 1.
    property real ledScreenOffLevel: 0.025
    property Item source: null
    /// Where `source` sits, in this item's coordinates.
    property rect sourceRect: Qt.rect(0, 0, root.width, root.height)

    ShaderEffect {
        id: ledScreenEffect

        readonly property size faceCells: Qt.size(Math.max(1, Math.round(root.sourceRect.width / ledScreenEffect.ledScreenLedSize)), Math.max(1, Math.round(root.sourceRect.height / ledScreenEffect.ledScreenLedSize)))
        readonly property rect faceRect: root.sourceRect
        readonly property color ledScreenLedColor: root.ledScreenLedColor
        readonly property real ledScreenLedSize: Math.max(1, root.ledScreenLedSize)
        readonly property real ledScreenOffLevel: root.ledScreenOffLevel
        readonly property var scaledSourceImage: scaledSource

        anchors.fill: parent
        blending: true
        fragmentShader: '/shaders/kerfur.frag.qsb'
        layer.enabled: true
        layer.smooth: true
        vertexShader: '/shaders/kerfur.vert.qsb'

        // One texel per cell, so the shader reads each cell's colour once.
        ShaderEffectSource {
            id: scaledSource

            hideSource: true
            sourceItem: root.source
            textureSize: ledScreenEffect.faceCells
        }
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
