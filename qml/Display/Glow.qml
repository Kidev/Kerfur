import QtQuick

Item {
    id: root

    property real blurMultiplier: 5
    property int glowBlendMode: 1
    property real glowBloom: 1.0
    property real glowBlurAmount: 0.4
    property color glowColor: Qt.rgba(0.757, 0, 0.697002, 1)
    property real glowMaxBrightness: 2
    property Item source: null

    BlurHelper {
        id: blurHelper

        anchors.fill: parent
        blurMultiplier: root.blurMultiplier
        source: root.source
    }

    ShaderEffect {
        readonly property alias blurMultiplier: root.blurMultiplier
        readonly property alias glowBlendMode: root.glowBlendMode
        readonly property alias glowBloom: root.glowBloom
        readonly property alias glowBlurAmount: root.glowBlurAmount
        readonly property alias glowColor: root.glowColor
        readonly property alias glowMaxBrightness: root.glowMaxBrightness
        readonly property alias iSource: root.source
        readonly property alias iSourceBlur1: blurHelper.blurSrc1
        readonly property alias iSourceBlur2: blurHelper.blurSrc2
        readonly property alias iSourceBlur3: blurHelper.blurSrc3
        readonly property alias iSourceBlur4: blurHelper.blurSrc4
        readonly property alias iSourceBlur5: blurHelper.blurSrc5

        anchors.fill: parent
        fragmentShader: '/shaders/glow.frag.qsb'
        vertexShader: '/shaders/glow.vert.qsb'
    }
}
