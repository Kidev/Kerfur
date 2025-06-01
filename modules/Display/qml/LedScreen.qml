import QtQuick
import Tools

Item {
    id: root

    property real blurMultiplier: root.settings.blurMultiplier
    property int glowBlendMode: root.settings.glowBlendMode
    property real glowBloom: root.settings.glowBloom
    property real glowBlurAmount: root.settings.glowBlurAmount
    property color glowColor: root.settings.glowColor
    property real glowMaxBrightness: root.settings.glowMaxBrightness
    property color ledScreenLedColor: root.settings.glowColor
    property real ledScreenLedSize: root.settings.ledScreenLedSize
    required property KSettings settings
    property Item source: null

    ShaderEffect {
        id: ledScreenEffect

        readonly property vector3d iResolution: Qt.vector3d(root.width, root.height, 1.0)
        readonly property alias iSource: root.source
        readonly property alias ledScreenGridStep: root.ledScreenLedSize
        readonly property alias ledScreenLedColor: root.ledScreenLedColor
        readonly property alias ledScreenLedSize: root.ledScreenLedSize
        property var scaledSourceImage: scaledSource

        anchors.fill: parent
        blending: true
        fragmentShader: '/shaders/kerfur.frag.qsb'
        layer.enabled: true
        layer.smooth: true
        vertexShader: '/shaders/kerfur.vert.qsb'

        ShaderEffectSource {
            id: scaledSource

            hideSource: true
            sourceItem: root.source
            textureSize: Qt.size(root.source.width / root.ledScreenLedSize, root.source.height
                                 / root.ledScreenLedSize)
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
