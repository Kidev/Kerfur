pragma Singleton

import QtCore
import QtQuick

KSettingsValues {
    id: root

    /// The settings as declared, before anything was loaded or changed.
    readonly property KSettingsValues defaults: KSettingsValues {}
    property Settings behaviour: Settings {
        id: settingsBehaviour

        property alias crossEyeDistance: root.crossEyeDistance
        property alias greetAfterHours: root.greetAfterHours
        property alias greetingEnabled: root.greetingEnabled
        property alias lookAheadAfterSeconds: root.lookAheadAfterSeconds
        property alias sleepTimeFactor: root.sleepTimeFactor

        category: "Behaviour"
    }
    property Settings general: Settings {
        id: settingsGeneral

        property alias allowEscapeShortcut: root.allowEscapeShortcut

        category: "General"
    }
    property Settings perception: Settings {
        id: settingsPerception

        property alias analysisMaxSide: root.analysisMaxSide
        property alias cameraHeight: root.cameraHeight
        property alias cameraId: root.cameraId
        property alias cameraTilt: root.cameraTilt
        property alias captureResolution: root.captureResolution
        property alias expressionEnabled: root.expressionEnabled
        property alias faceConfidence: root.faceConfidence
        property alias fieldOfView: root.fieldOfView
        property alias idleAfterSeconds: root.idleAfterSeconds
        property alias handConfidence: root.handConfidence
        property alias idleWatch: root.idleWatch
        property alias identityThreshold: root.identityThreshold
        property alias keypointConfidence: root.keypointConfidence
        property alias loneStrangerHands: root.loneStrangerHands
        property alias objectConfidence: root.objectConfidence
        property alias palmConfidence: root.palmConfidence
        property alias personConfidence: root.personConfidence
        property alias previewMaxSide: root.previewMaxSide
        property alias probeSeconds: root.probeSeconds
        property alias screenOffset: root.screenOffset
        property alias trackingSmoothing: root.trackingSmoothing
        property alias visionUpdateHz: root.visionUpdateHz
        property alias wakePercent: root.wakePercent
        property alias watchHz: root.watchHz

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
        property alias glowCustomColor: root.glowCustomColor
        property alias glowMaxBrightness: root.glowMaxBrightness
        property alias ledScreenFadeIn: root.ledScreenFadeIn
        property alias ledScreenFadeOut: root.ledScreenFadeOut
        property alias ledScreenLedSize: root.ledScreenLedSize
        property alias ledScreenOffLevel: root.ledScreenOffLevel
        property alias ledScreenRowGap: root.ledScreenRowGap
        property alias ledScreenStripeGap: root.ledScreenStripeGap
        property alias ledScreenWhiteCore: root.ledScreenWhiteCore
        property alias leftRightAngle: root.leftRightAngle
        property alias maskSpreadAtMax: root.maskSpreadAtMax
        property alias maskSpreadAtMin: root.maskSpreadAtMin
        property alias maskThresholdMax: root.maskThresholdMax
        property alias maskThresholdMin: root.maskThresholdMin
        property alias showControls: root.showControls
        property alias upDownAngle: root.upDownAngle

        category: "Shader"
    }
    property Settings speech: Settings {
        id: settingsSpeech

        property alias speechRecognitionDebug: root.speechRecognitionDebug
        property alias speechRecognitionEnabled: root.speechRecognitionEnabled

        category: "Speech"
    }

    /// Puts the setting `name` back to its default value.
    function resetToDefault(name) {
        root[name] = root.defaults[name];
    }

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
