import QtQuick
import QtQuick.Window
import QtMultimedia

Window {
    id: root

    readonly property int blinkDuration: 150
    readonly property real doubleBlinkChance: 0.2
    readonly property int doubleBlinkDuration: 200
    property bool isBlinking: false
    property bool isTouched: false
    readonly property int ledSize: 16
    readonly property int maxBlinkInterval: 8000
    readonly property real meowVolume: 1
    readonly property int minBlinkInterval: 2000
    readonly property string pathEyesClosed: "qrc:/assets/img/eyes_closed.png"
    readonly property string pathEyesMeow: "qrc:/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "qrc:/assets/img/eyes_opened.png"
    readonly property string pathSoundMeow: "qrc:/assets/sound/meow.wav"

    function playMeowSound() {
        meowSound.play();
    }

    function requestExit() {
        Qt.quit();
    }

    function resetBlinkTimer() {
        blinkTimer.interval = Math.floor(Math.random() * (root.maxBlinkInterval - root.minBlinkInterval)) + root.minBlinkInterval;
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

    color: "black"
    visibility: Window.FullScreen
    visible: true

    Component.onCompleted: {
        root.resetBlinkTimer();
    }

    Shortcut {
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    SoundEffect {
        id: meowSound

        source: root.pathSoundMeow
        volume: root.meowVolume
    }

    Image {
        id: displayImage

        anchors.centerIn: parent
        fillMode: Image.PreserveAspectFit
        height: Math.min(root.height, root.width * (displayImage.sourceSize.height / displayImage.sourceSize.width))
        source: root.isTouched ? root.pathEyesMeow : root.isBlinking ? root.pathEyesClosed : root.pathEyesOpened
        width: Math.min(root.width, root.height * (displayImage.sourceSize.width / displayImage.sourceSize.height))

        onStatusChanged: {
            if (displayImage.status === Image.Error) {
                console.error("Failed to load image:", displayImage.source);
            }
        }
    }

    LedScreen {
        anchors.fill: displayImage
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
