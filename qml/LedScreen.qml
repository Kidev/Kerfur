import QtQuick

Item {
    id: root

    property color ledScreenLedColor: Qt.rgba(1, 1, 1, 1)
    property real ledScreenLedSize: 20
    property Item source: null

    ShaderEffect {
        readonly property vector3d iResolution: Qt.vector3d(root.width, root.height, 1.0)
        readonly property alias iSource: root.source
        readonly property alias ledScreenLedColor: root.ledScreenLedColor
        readonly property alias ledScreenLedSize: root.ledScreenLedSize
        property var scaledSourceImage: scaledSource

        anchors.fill: parent
        blending: true
        fragmentShader: 'qrc:/shaders/kerfur.frag.qsb'
        vertexShader: 'qrc:/shaders/kerfur.vert.qsb'

        ShaderEffectSource {
            id: scaledSource

            hideSource: true
            sourceItem: root.source
            textureSize: Qt.size(root.source.width / root.ledScreenLedSize, root.source.height / root.ledScreenLedSize)
        }
    }
}
