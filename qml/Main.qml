import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Effects
import qml.Tools
import qml.Display

Window {
    id: root

    readonly property int blinkDuration: 150
    readonly property real doubleBlinkChance: 0.1
    readonly property int doubleBlinkDuration: 200
    property bool isBlinking: false
    property bool isTouched: false
    property alias kSettings: settings
    readonly property point leftEyeCenter: Qt.point(32, 22)
    readonly property real leftOffset: ((settings.leftRightAngle - 90) / 90) * root.maxPupilMovement
    property point leftPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    readonly property int maxBlinkInterval: 8000
    readonly property int maxPupilMovement: 5
    readonly property real meowVolume: 1
    readonly property int minBlinkInterval: 4000
    readonly property string pathEyesClosed: "/assets/img/eyes_closed.png"
    readonly property string pathEyesMeow: "/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "/assets/img/eyes_opened.png"
    readonly property string pathEyesPupil: "/assets/img/pupil.png"
    readonly property string pathSoundMeow: "/assets/sound/meow.wav"
    readonly property point rightEyeCenter: Qt.point(90, 22)
    readonly property real rightOffset: ((settings.upDownAngle - 90) / 90) * root.maxPupilMovement
    property point rightPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool showControls: false
    property bool winkLeft: false
    property bool winkRight: false

    function playMeowSound() {
        meowSound.play();
    }

    function requestExit() {
        Qt.quit();
    }

    function resetBlinkTimer() {
        blinkTimer.interval = Math.floor(Math.random() * (root.maxBlinkInterval
                                                          - root.minBlinkInterval))
                + root.minBlinkInterval;
        blinkTimer.restart();
    }

    function startBlinking() {
        if (!root.isTouched) {
            root.isBlinking = true;
            blinkDurationTimer.start();
        } else {
            root.resetBlinkTimer();
        }
    }

    function updatePupilPositions(leftRight, upDown) {
        settings.leftRightAngle = Math.max(0, Math.min(180, leftRight));
        settings.upDownAngle = Math.max(0, Math.min(180, upDown));
    }

    color: "black"
    visibility: Window.FullScreen
    visible: true

    Component.onCompleted: {
        root.resetBlinkTimer();
        root.updatePupilPositions(settings.leftRightAngle, settings.upDownAngle);
    }

    KSettings {
        id: settings

    }

    Shortcut {
        enabled: false
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    Shortcut {
        enabled: true
        sequence: "Tab"

        onActivated: root.showControls = !root.showControls
    }

    SoundEffect {
        id: meowSound

        source: root.pathSoundMeow
        volume: root.meowVolume
    }

    Item {
        id: displayContainer

        property real relativeScaleHeight: displayImage.height / (displayImage.sourceSize.height)
        property real relativeScaleWidth: displayImage.width / (displayImage.sourceSize.width)

        anchors.centerIn: parent
        height: Math.min(root.height, root.width * (displayImage.sourceSize.height
                                                    / displayImage.sourceSize.width))
        width: Math.min(root.width, root.height * (displayImage.sourceSize.width
                                                   / displayImage.sourceSize.height))

        Image {
            id: displayImage

            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: root.isTouched ? root.pathEyesMeow : root.isBlinking ? root.pathEyesClosed :
                                                                           root.pathEyesOpened

            onStatusChanged: {
                if (displayImage.status === Image.Error) {
                    console.error("Failed to load image:", displayImage.source);
                }
            }
        }

        Image {
            id: leftPupil

            height: leftPupil.sourceSize.height * displayContainer.relativeScaleHeight
            source: root.pathEyesPupil
            visible: !root.winkLeft
            width: leftPupil.sourceSize.width * displayContainer.relativeScaleWidth
            x: displayContainer.relativeScaleWidth * (root.leftEyeCenter.x + root.leftPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.leftEyeCenter.y
                                                       + root.leftPupilOffset.y)
        }

        Image {
            id: rightPupil

            height: rightPupil.sourceSize.height * displayContainer.relativeScaleHeight
            source: root.pathEyesPupil
            visible: !root.winkRight
            width: rightPupil.sourceSize.width * displayContainer.relativeScaleWidth
            x: displayContainer.relativeScaleWidth * (root.rightEyeCenter.x
                                                      + root.rightPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.rightEyeCenter.y
                                                       + root.rightPupilOffset.y)
        }
    }

    MultiEffect {
        id: maskedDisplayImage

        anchors.fill: displayContainer
        autoPaddingEnabled: settings.autoPaddingEnabled
        layer.enabled: true
        maskEnabled: !root.isBlinking && !root.isTouched
        maskInverted: true
        maskSpreadAtMax: settings.maskSpreadAtMax
        maskSpreadAtMin: settings.maskSpreadAtMin
        maskThresholdMax: settings.maskThresholdMax
        maskThresholdMin: settings.maskThresholdMin

        maskSource: ShaderEffectSource {
            hideSource: true
            sourceItem: displayContainer
        }
        source: ShaderEffectSource {
            hideSource: true
            sourceItem: displayImage
        }
    }

    LedScreen {
        id: ledScreen

        anchors.fill: maskedDisplayImage
        blurMultiplier: settings.blurMultiplier
        glowBlendMode: settings.glowBlendMode
        glowBloom: settings.glowBloom
        glowBlurAmount: settings.glowBlurAmount
        glowColor: settings.glowColor
        glowMaxBrightness: settings.glowMaxBrightness
        ledScreenLedColor: settings.glowColor
        ledScreenLedSize: settings.ledScreenLedSize
        source: maskedDisplayImage
    }

    Timer {
        id: blinkTimer

        interval: root.minBlinkInterval
        repeat: true
        running: true

        onTriggered: root.startBlinking()
    }

    Timer {
        id: blinkDurationTimer

        interval: root.blinkDuration
        repeat: false
        running: false

        onTriggered: {
            root.isBlinking = false;

            if (Math.random() < root.doubleBlinkChance) {
                doubleBlinkTimer.start();
            } else {
                root.resetBlinkTimer();
            }
        }
    }

    Timer {
        id: doubleBlinkTimer

        interval: root.doubleBlinkDuration
        repeat: false
        running: false

        onTriggered: {
            if (!root.isTouched) {
                root.isBlinking = true;
                blinkDurationTimer.start();
            }
            root.resetBlinkTimer();
        }
    }

    MultiPointTouchArea {
        id: touchArea

        anchors.fill: parent

        onPressed: {
            root.isTouched = true;
            root.playMeowSound();
            blinkTimer.stop();
        }
        onReleased: {
            root.isTouched = false;

            root.resetBlinkTimer();
        }
    }

    MouseArea {
        id: pupilTrackingArea

        property bool shouldTrack: !root.isBlinking && !root.isTouched && !pressed

        anchors.fill: parent
        cursorShape: root.showControls ? Qt.ArrowCursor : Qt.BlankCursor
        hoverEnabled: true
        propagateComposedEvents: true

        onMouseXChanged: {
            if (pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                settings.leftRightAngle = (pupilTrackingArea.mouseX / pupilTrackingArea.width)
                        * 180;
            }
        }
        onMouseYChanged: {
            if (pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                settings.upDownAngle = (pupilTrackingArea.mouseY / pupilTrackingArea.height) * 180;
            }
        }
        onPressed: mouse => {
                       mouse.accepted = false;
                   }
        onReleased: mouse => {
                        mouse.accepted = false;
                    }
    }

    ControlPanel {
        id: controlPanel

        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height
        settings: settings
        visible: root.showControls
        width: 150
    }
}
