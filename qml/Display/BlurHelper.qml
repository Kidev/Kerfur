import QtQuick

Item {
    id: root

    readonly property int blurMax: 64
    property real blurMultiplier: 5
    property alias blurSrc1: blurredItemSource1
    property alias blurSrc2: blurredItemSource2
    property alias blurSrc3: blurredItemSource3
    property alias blurSrc4: blurredItemSource4
    property alias blurSrc5: blurredItemSource5
    property Item source: null

    BlurItem {
        id: blurredItemSource1

        height: blurredItemSource1.src ? Math.ceil(blurredItemSource1.src.height / 16)
                                         * blurredItemSource1.blurItemSize : 0
        multiplier: root.blurMultiplier
        src: root.source
        width: blurredItemSource1.src ? Math.ceil(blurredItemSource1.src.width / 16)
                                        * blurredItemSource1.blurItemSize : 0
    }

    BlurItem {
        id: blurredItemSource2

        height: blurredItemSource1.height * 0.5
        multiplier: root.blurMultiplier
        src: root.blurMax > 2 ? blurredItemSource1 : null
        width: blurredItemSource1.width * 0.5
    }

    BlurItem {
        id: blurredItemSource3

        height: blurredItemSource2.height * 0.5
        multiplier: root.blurMultiplier
        src: root.blurMax > 8 ? blurredItemSource2 : null
        width: blurredItemSource2.width * 0.5
    }

    BlurItem {
        id: blurredItemSource4

        height: blurredItemSource3.height * 0.5
        multiplier: root.blurMultiplier
        src: root.blurMax > 16 ? blurredItemSource3 : null
        width: blurredItemSource3.width * 0.5
    }

    BlurItem {
        id: blurredItemSource5

        height: blurredItemSource4.height * 0.5
        multiplier: root.blurMultiplier
        src: root.blurMax > 32 ? blurredItemSource4 : null
        width: blurredItemSource4.width * 0.5
    }

    component BlurItem: ShaderEffect {
        id: self

        readonly property int blurItemSize: 8
        property real multiplier: 5
        property vector2d offset: Qt.vector2d((1.0 + self.multiplier) / self.width, (1.0
                                                                                     + self.multiplier)
                                              / self.height)
        property Item src: null

        fragmentShader: ":/shaders/bluritems.frag.qsb"
        layer.enabled: true
        layer.smooth: true
        vertexShader: ":/shaders/bluritems.vert.qsb"
        visible: false
    }
}
