import QtQuick

QtObject {
    id: root

    property bool autoPaddingEnabled: true
    property real blurMultiplier: 1
    property int glowBlendMode: 1
    property real glowBloom: 1.0
    property real glowBlurAmount: 0.4
    property color glowColor: "magenta"
    property real glowMaxBrightness: 0.6
    property int ledScreenLedSize: 8
    property real maskSpreadAtMax: 0
    property real maskSpreadAtMin: 0
    property real maskThresholdMax: 1.0
    property real maskThresholdMin: 0.25
}
