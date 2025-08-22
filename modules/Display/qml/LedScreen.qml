import QtQuick
import Tools
import qml 1.0

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
    property Item source: null

    ShaderEffect {
        id: ledScreenEffect

        readonly property vector3d iResolution: Qt.vector3d(root.width, root.height, 1.0)
        readonly property alias iSource: root.source
        property alias ledScreenGridStep: root.ledScreenLedSize
        readonly property alias ledScreenLedColor: root.ledScreenLedColor
        property alias ledScreenLedSize: root.ledScreenLedSize
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
            textureSize: Qt.size(root.source.width / root.ledScreenLedSize, root.source.height / root.ledScreenLedSize)
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
