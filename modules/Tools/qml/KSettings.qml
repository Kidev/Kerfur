import QtQuick

QtObject {
    id: root

    property bool autoPaddingEnabled: true
    property real blurMultiplier: 1
    property bool faceTrackingDebug: false
    property bool faceTrackingEnabled: false
    property real faceTrackingSensitivity: 1.0
    property int glowBlendMode: 1
    property real glowBloom: 1.0
    property real glowBlurAmount: 0.4
    property color glowColor: "magenta"
    property real glowMaxBrightness: 0.6
    property int ledScreenLedSize: 9
    property real leftRightAngle: 90
    property real maskSpreadAtMax: 0
    property real maskSpreadAtMin: 0
    property real maskThresholdMax: 1.0
    property real maskThresholdMin: 0.25
    property bool showControls: false
    property real upDownAngle: 90

    Behavior on leftRightAngle {
        enabled: false

        NumberAnimation {
            duration: 300
            easing.type: Easing.OutQuad
        }
    }
    Behavior on upDownAngle {
        enabled: false

        NumberAnimation {
            duration: 300
            easing.type: Easing.OutQuad
        }
    }
}
