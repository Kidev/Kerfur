pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: root

    property bool allowEscapeShortcut: true
    property bool autoPaddingEnabled: true
    property real blurMultiplier: 1
    property real eyeMovementRange: 1.0
    property bool faceTrackingDebug: false
    property bool faceTrackingEnabled: false
    property Settings general: Settings {
        id: settingsGeneral

        property alias allowEscapeShortcut: root.allowEscapeShortcut

        category: "General"
    }
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
    property Settings shader: Settings {
        id: settingsShader

        property alias autoPaddingEnabled: root.autoPaddingEnabled
        property alias blurMultiplier: root.blurMultiplier
        property alias eyeMovementRange: root.eyeMovementRange
        property alias faceTrackingDebug: root.faceTrackingDebug
        property alias faceTrackingEnabled: root.faceTrackingEnabled
        property alias glowBlendMode: root.glowBlendMode
        property alias glowBloom: root.glowBloom
        property alias glowBlurAmount: root.glowBlurAmount
        property alias glowColor: root.glowColor
        property alias glowMaxBrightness: root.glowMaxBrightness
        property alias ledScreenLedSize: root.ledScreenLedSize
        property alias leftRightAngle: root.leftRightAngle
        property alias maskSpreadAtMax: root.maskSpreadAtMax
        property alias maskSpreadAtMin: root.maskSpreadAtMin
        property alias maskThresholdMax: root.maskThresholdMax
        property alias maskThresholdMin: root.maskThresholdMin
        property alias showControls: root.showControls
        property alias upDownAngle: root.upDownAngle

        category: "Shader"
    }
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
