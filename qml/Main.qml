import QtQuick
import QtQuick.Window
import QtMultimedia

Window {
    id: root

    readonly property int blinkDuration: 150         // How long eyes stay closed (ms)
    readonly property real doubleBlinkChance: 0.2    // 20% chance of a double blink

    readonly property int doubleBlinkDuration: 200   // Time between blinks in a double-blink
    readonly property vector4d glowColor: Qt.rgba(1.0, 1.0, 1.0, 1.0)
    property bool isBlinking: false
    property bool isTouched: false
    readonly property int ledSize: 16
    readonly property int maxBlinkInterval: 8000     // Maximum time between blinks (ms)
    readonly property int minBlinkInterval: 2000     // Minimum time between blinks (ms)
    readonly property string pathEyesClosed: "qrc:/assets/img/eyes_closed.png"
    readonly property string pathEyesMeow: "qrc:/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "qrc:/assets/img/eyes_opened.png"
    readonly property string pathSoundMeow: "qrc:/assets/sound/meow.mp3"

    function playMeowSound() {
        meowSound.play();
    }

    function requestExit() {
        Qt.quit();
    }

    function resetBlinkTimer() {
        blinkTimer.interval = Math.floor(Math.random() * (maxBlinkInterval - minBlinkInterval)) + minBlinkInterval;
        blinkTimer.restart();
    }

    function startBlinking() {
        if (!root.isTouched) {
            root.isBlinking = true;
            blinkDurationTimer.start();
        } else {
            resetBlinkTimer();
        }
    }

    color: "black"  // Black background for the unfilled areas

    visibility: Window.FullScreen
    visible: true

    Component.onCompleted: {
        root.resetBlinkTimer();
    }

    Shortcut {
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    MediaPlayer {
        id: meowSound

        source: root.pathSoundMeow

        audioOutput: AudioOutput {
            volume: 1
        }
    }

    Image {
        id: displayImage

        anchors.centerIn: parent
        fillMode: Image.PreserveAspectFit
        height: Math.min(parent.height, parent.width * (sourceSize.height / sourceSize.width))
        source: root.isTouched ? root.pathEyesMeow : root.isBlinking ? root.pathEyesClosed : root.pathEyesOpened
        width: Math.min(parent.width, parent.height * (sourceSize.width / sourceSize.height))

        onStatusChanged: {
            if (status === Image.Error) {
                console.error("Failed to load image:", source);
            }
        }
    }

    LedScreen {
        anchors.fill: displayImage
        ledScreenLedColor: root.glowColor
        ledScreenLedSize: root.ledSize
        source: displayImage
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

        onPressed: touchPoints => {
            if (touchPoints.length >= 4) {
                root.requestExit();
            } else {
                root.isTouched = true;
                root.playMeowSound();
            }
        }
        onReleased: {
            root.isTouched = false;
        }
    }
}
