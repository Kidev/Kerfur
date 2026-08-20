pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: root

    property bool allowEscapeShortcut: true
    /// The longer side the camera frame is reduced to before the vision models
    /// see it. Persisted because it is a property of the machine Kerfur is
    /// running on, not of the session.
    property int analysisMaxSide: 960
    readonly property string appDataPath: StandardPaths.writableLocation(StandardPaths.AppDataLocation)
    property bool autoPaddingEnabled: true
    property real blurMultiplier: 1
    property real eyeMovementRange: 1.0
    property bool faceTrackingDebug: false
    property bool faceTrackingEnabled: false
    property string facesGalleryPath: root.appDataPath + "/faces/"
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
    property string modelsPath: root.appDataPath + "/models/"
    property Settings perception: Settings {
        id: settingsPerception

        property alias analysisMaxSide: root.analysisMaxSide

        category: "Perception"
    }
    property Settings paths: Settings {
        id: settingsPaths

        property alias facesGalleryPath: root.facesGalleryPath
        property alias modelsPath: root.modelsPath

        category: "Paths"
    }
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
    property Settings speech: Settings {
        id: settingsSpeech

        property alias speechRecognitionDebug: root.speechRecognitionDebug
        property alias speechRecognitionEnabled: root.speechRecognitionEnabled

        category: "Speech"
    }
    property bool speechRecognitionDebug: false
    property bool speechRecognitionEnabled: false
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
