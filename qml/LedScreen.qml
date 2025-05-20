import QtQuick

Item {
    id: root

    property real blurMultiplier: 1
    property int glowBlendMode: 1
    property real glowBloom: 1.0
    property real glowBlurAmount: 0.4
    property color glowColor: Qt.rgba(0.757, 0, 0.697002, 1)
    property real glowMaxBrightness: 0.6
    property color ledScreenLedColor: Qt.rgba(1, 1, 1, 1)
    property real ledScreenLedSize: 8
    property Item source: null

    ShaderEffect {
        id: ledScreenEffect

        readonly property vector3d iResolution: Qt.vector3d(root.width, root.height, 1.0)
        readonly property alias iSource: root.source
        readonly property alias ledScreenLedColor: root.ledScreenLedColor
        readonly property alias ledScreenLedSize: root.ledScreenLedSize
        property var scaledSourceImage: scaledSource

        anchors.fill: parent
        blending: true
        fragmentShader: 'qrc:/shaders/kerfur.frag.qsb'
        layer.enabled: true
        layer.smooth: true
        vertexShader: 'qrc:/shaders/kerfur.vert.qsb'

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
