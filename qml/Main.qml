import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Controls

Window {
    id: root

    readonly property int blinkDuration: 150
    readonly property real doubleBlinkChance: 0.2
    readonly property int doubleBlinkDuration: 200
    property bool isBlinking: false
    property bool isTouched: false
    readonly property int maxBlinkInterval: 8000
    readonly property real meowVolume: 1
    readonly property int minBlinkInterval: 2000
    readonly property string pathEyesClosed: "qrc:/assets/img/eyes_closed.png"
    readonly property string pathEyesMeow: "qrc:/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "qrc:/assets/img/eyes_opened.png"
    readonly property string pathEyesPupil: "qrc:/assets/img/eyes_pupil.png"
    readonly property string pathSoundMeow: "qrc:/assets/sound/meow.wav"
    property bool showControls: false

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

    Shortcut {
        enabled: false
        sequence: "Tab"

        onActivated: root.showControls = !root.showControls
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
        id: ledScreen

        anchors.fill: displayImage
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

    // CONTROLS
    // Control panel for shader parameters

    Rectangle {
        id: controlPanel

        anchors.right: parent.right
        anchors.top: parent.top
        color: "#333333"
        height: parent.height
        opacity: 0.7
        visible: root.showControls
        width: 150
        z: 100

        Flickable {
            anchors.fill: parent
            clip: true
            contentHeight: controlsColumn.height
            contentWidth: parent.width

            Column {
                spacing: 1

                Text {
                    color: "white"
                    font.bold: true
                    font.pointSize: 12
                    text: "kerfur"
                }

                Text {
                    color: "white"
                    font.italic: true
                    font.pointSize: 8
                    text: "kidev"
                    topPadding: -5
                }
            }

            Column {
                id: controlsColumn

                padding: 10
                spacing: 10
                topPadding: 50
                width: parent.width

                // LED Size slider
                Text {
                    color: "white"
                    text: "LED Size: " + ledSizeSlider.value.toFixed(1)
                }

                Slider {
                    id: ledSizeSlider

                    from: 1
                    to: 50
                    value: 8
                    width: parent.width - 20

                    onValueChanged: ledScreen.ledScreenLedSize = value
                }

                // Blur Multiplier slider
                Text {
                    color: "white"
                    text: "Blur Mult: " + blurMultiplierSlider.value.toFixed(2)
                }

                Slider {
                    id: blurMultiplierSlider

                    from: 0
                    to: 5
                    value: 5
                    width: parent.width - 20

                    onValueChanged: ledScreen.blurMultiplier = value
                }

                // Blur Max slider
                Text {
                    color: "white"
                    text: "Blur Max: " + blurMaxSlider.value
                }

                Slider {
                    id: blurMaxSlider

                    from: 2
                    stepSize: 1
                    to: 64
                    value: 64
                    width: parent.width - 20

                    onValueChanged: ledScreen.blurMax = value
                }

                // Glow Blend Mode
                Text {
                    color: "white"
                    text: "Blend Mode: " + ["Additive", "Screen", "Replace", "Outer"][glowBlendModeSlider.value]
                }

                Slider {
                    id: glowBlendModeSlider

                    from: 0
                    stepSize: 1
                    to: 3
                    value: 1
                    width: parent.width - 20

                    onValueChanged: ledScreen.glowBlendMode = value
                }

                // Glow Bloom slider
                Text {
                    color: "white"
                    text: "Glow Bloom: " + glowBloomSlider.value.toFixed(2)
                }

                Slider {
                    id: glowBloomSlider

                    from: 0
                    to: 2
                    value: 1.0
                    width: parent.width - 20

                    onValueChanged: ledScreen.glowBloom = value
                }

                // Glow Blur Amount slider
                Text {
                    color: "white"
                    text: "Glow Blur: " + glowBlurAmountSlider.value.toFixed(3)
                }

                Slider {
                    id: glowBlurAmountSlider

                    from: 0
                    to: 1
                    value: 0.4
                    width: parent.width - 20

                    onValueChanged: ledScreen.glowBlurAmount = value
                }

                // Glow Max Brightness slider
                Text {
                    color: "white"
                    text: "Glow Max: " + glowMaxBrightnessSlider.value.toFixed(2)
                }

                Slider {
                    id: glowMaxBrightnessSlider

                    from: 0
                    to: 2
                    value: 2
                    width: parent.width - 20

                    onValueChanged: ledScreen.glowMaxBrightness = value
                }

                // Glow Color
                Text {
                    color: "white"
                    text: "Glow Color"
                }

                Row {
                    spacing: 5

                    Rectangle {
                        border.color: "gray"
                        color: "#C100B2"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: ledScreen.glowColor = Qt.rgba(0.757, 0, 0.697002, 1)
                        }
                    }

                    Rectangle {
                        border.color: "gray"
                        color: "red"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: ledScreen.glowColor = Qt.rgba(1, 0, 0, 1)
                        }
                    }

                    Rectangle {
                        border.color: "gray"
                        color: "green"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: ledScreen.glowColor = Qt.rgba(0, 1, 0, 1)
                        }
                    }

                    Rectangle {
                        border.color: "gray"
                        color: "blue"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: ledScreen.glowColor = Qt.rgba(0, 0, 1, 1)
                        }
                    }
                }
            }
        }
    }
}
