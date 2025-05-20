import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Controls
import QtQuick.Effects

Window {
    id: root

    readonly property int blinkDuration: 150
    readonly property real doubleBlinkChance: 0.1
    readonly property int doubleBlinkDuration: 200
    property bool isBlinking: false
    property bool isTouched: false
    property alias kSettings: settings
    readonly property point leftEyeCenter: Qt.point(32, 22)
    property point leftPupilOffset: Qt.point(((root.leftRightAngle - 90) / 90) * root.maxPupilMovement, ((root.upDownAngle - 90) / 90) * root.maxPupilMovement)
    property real leftRightAngle: 90
    readonly property int maxBlinkInterval: 8000
    readonly property int maxPupilMovement: 5
    readonly property real meowVolume: 1
    readonly property int minBlinkInterval: 4000
    readonly property string pathEyesClosed: "qrc:/assets/img/eyes_closed.png"
    readonly property string pathEyesMeow: "qrc:/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "qrc:/assets/img/eyes_opened.png"
    readonly property string pathEyesPupil: "qrc:/assets/img/pupil.png"
    readonly property string pathSoundMeow: "qrc:/assets/sound/meow.wav"
    readonly property point rightEyeCenter: Qt.point(90, 22)
    property point rightPupilOffset: root.leftPupilOffset
    property bool showControls: false
    property real upDownAngle: 90
    property bool winkLeft: false
    property bool winkRight: false

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

    function updatePupilPositions(leftRight, upDown) {
        root.leftRightAngle = Math.max(0, Math.min(180, leftRight));
        root.upDownAngle = Math.max(0, Math.min(180, upDown));
    }

    color: "black"
    visibility: Window.Maximized
    visible: true

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

    Component.onCompleted: {
        root.resetBlinkTimer();
        root.updatePupilPositions(leftRightAngle, upDownAngle);
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
        height: Math.min(root.height, root.width * (displayImage.sourceSize.height / displayImage.sourceSize.width))
        width: Math.min(root.width, root.height * (displayImage.sourceSize.width / displayImage.sourceSize.height))

        Image {
            id: displayImage

            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: root.isTouched ? root.pathEyesMeow : root.isBlinking ? root.pathEyesClosed : root.pathEyesOpened

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
            y: displayContainer.relativeScaleHeight * (root.leftEyeCenter.y + root.leftPupilOffset.y)
        }

        Image {
            id: rightPupil

            height: rightPupil.sourceSize.height * displayContainer.relativeScaleHeight
            source: root.pathEyesPupil
            visible: !root.winkRight
            width: rightPupil.sourceSize.width * displayContainer.relativeScaleWidth
            x: displayContainer.relativeScaleWidth * (root.rightEyeCenter.x + root.rightPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.rightEyeCenter.y + root.rightPupilOffset.y)
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
        hoverEnabled: true
        propagateComposedEvents: true

        onMouseXChanged: {
            if (shouldTrack && containsMouse) {
                root.leftRightAngle = (mouseX / width) * 180;
            }
        }
        onMouseYChanged: {
            if (shouldTrack && containsMouse) {
                root.upDownAngle = (mouseY / height) * 180;
            }
        }
        onPressed: mouse => {
            mouse.accepted = false;
        }
        onReleased: mouse => {
            mouse.accepted = false;
        }
    }

    // Control panel for shader parameters
    Timer {
        id: colorAnimationTimer

        property real hue: 0

        interval: 16 // approximately 60 FPS
        repeat: true
        running: false

        onTriggered: {
            hue = (hue + 0.005) % 1.0;
            settings.glowColor = Qt.hsva(hue, 1.0, 1.0, 1.0);
        }
    }

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

                KSlider {
                    id: ledSizeSlider

                    from: 1
                    settingName: "ledScreenLedSize"
                    settingsObject: settings
                    to: 50
                    width: parent.width - 20
                }

                // Blur Multiplier slider
                Text {
                    color: "white"
                    text: "Blur Mult: " + blurMultiplierSlider.value.toFixed(2)
                }

                KSlider {
                    id: blurMultiplierSlider

                    decimals: 2
                    from: 0
                    settingName: "blurMultiplier"
                    settingsObject: settings
                    to: 5
                    width: parent.width - 20
                }

                // Up down angle
                Text {
                    color: "white"
                    text: "Up/down angle: " + upDownAngleSlider.value.toFixed(0)
                }

                KSlider {
                    id: upDownAngleSlider

                    decimals: 0
                    from: 0
                    settingName: "upDownAngle"
                    settingsObject: root
                    to: 180
                    width: parent.width - 20
                }

                // Left right angle
                Text {
                    color: "white"
                    text: "Left/right angle: " + leftRightAngleSlider.value.toFixed(0)
                }

                KSlider {
                    id: leftRightAngleSlider

                    decimals: 0
                    from: 0
                    settingName: "leftRightAngle"
                    settingsObject: root
                    to: 180
                    width: parent.width - 20
                }

                // Spread min
                Text {
                    color: "white"
                    text: "Spread min: " + spreadMinSlider.value.toFixed(2)
                }

                KSlider {
                    id: spreadMinSlider

                    decimals: 2
                    from: 0.0
                    settingName: "maskSpreadAtMin"
                    settingsObject: settings
                    to: 1.0
                    width: parent.width - 20
                }

                // Spread max
                Text {
                    color: "white"
                    text: "Spread max: " + spreadMaxSlider.value.toFixed(2)
                }

                KSlider {
                    id: spreadMaxSlider

                    decimals: 2
                    from: 0.0
                    settingName: "maskSpreadAtMax"
                    settingsObject: settings
                    to: 1.0
                    width: parent.width - 20
                }

                // Threshold min
                Text {
                    color: "white"
                    text: "threshold min: " + thresholdMinSlider.value.toFixed(2)
                }

                KSlider {
                    id: thresholdMinSlider

                    decimals: 2
                    from: 0.0
                    settingName: "maskThresholdMin"
                    settingsObject: settings
                    to: 1.0
                    width: parent.width - 20
                }

                // Threshold max
                Text {
                    color: "white"
                    text: "Threshold max: " + thresholdMaxSlider.value.toFixed(2)
                }

                KSlider {
                    id: thresholdMaxSlider

                    decimals: 2
                    from: 0.0
                    settingName: "maskThresholdMax"
                    settingsObject: settings
                    to: 1.0
                    width: parent.width - 20
                }

                // Glow Blend Mode
                Text {
                    color: "white"
                    text: "Blend Mode: " + ["Additive", "Screen", "Replace", "Outer"][glowBlendModeSlider.value]
                }

                KSlider {
                    id: glowBlendModeSlider

                    decimals: 0
                    from: 0
                    settingName: "glowBlendMode"
                    settingsObject: settings
                    stepSize: 1
                    to: 3
                    width: parent.width - 20
                }

                // Glow Bloom slider
                Text {
                    color: "white"
                    text: "Glow Bloom: " + glowBloomSlider.value.toFixed(2)
                }

                KSlider {
                    id: glowBloomSlider

                    decimals: 2
                    from: 0
                    settingName: "glowBloom"
                    settingsObject: settings
                    to: 2
                    width: parent.width - 20
                }

                // Glow Blur Amount slider
                Text {
                    color: "white"
                    text: "Glow Blur: " + glowBlurAmountSlider.value.toFixed(3)
                }

                KSlider {
                    id: glowBlurAmountSlider

                    decimals: 3
                    from: 0
                    settingName: "glowBlurAmount"
                    settingsObject: settings
                    to: 1
                    width: parent.width - 20
                }

                // Glow Max Brightness slider
                Text {
                    color: "white"
                    text: "Glow Max: " + glowMaxBrightnessSlider.value.toFixed(2)
                }

                KSlider {
                    id: glowMaxBrightnessSlider

                    decimals: 2
                    from: 0
                    settingName: "glowMaxBrightness"
                    settingsObject: settings
                    to: 2
                    width: parent.width - 20
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
                        color: "cyan"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: {
                                colorAnimationTimer.running = false;
                                settings.glowColor = Qt.rgba(0, 1, 1, 1);
                            }
                        }
                    }

                    Rectangle {
                        border.color: "gray"
                        color: "green"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: {
                                colorAnimationTimer.running = false;
                                settings.glowColor = Qt.rgba(0, 1, 0, 1);
                            }
                        }
                    }

                    Rectangle {
                        border.color: "gray"
                        color: "magenta"
                        height: 30
                        width: 30

                        MouseArea {
                            anchors.fill: parent

                            onClicked: {
                                colorAnimationTimer.running = false;
                                settings.glowColor = Qt.rgba(1, 0, 1, 1);
                            }
                        }
                    }

                    Rectangle {
                        id: gamerColorRect

                        border.color: "gray"
                        height: 30
                        width: 30

                        gradient: Gradient {
                            GradientStop {
                                color: "red"
                                position: 0.0
                            }

                            GradientStop {
                                color: "yellow"
                                position: 0.2
                            }

                            GradientStop {
                                color: "green"
                                position: 0.4
                            }

                            GradientStop {
                                color: "cyan"
                                position: 0.6
                            }

                            GradientStop {
                                color: "blue"
                                position: 0.8
                            }

                            GradientStop {
                                color: "magenta"
                                position: 1.0
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            color: "white"
                            font.bold: true
                            font.pixelSize: 8
                            text: "GAMER"
                        }

                        MouseArea {
                            anchors.fill: parent

                            onClicked: {
                                colorAnimationTimer.running = true;
                            }
                        }
                    }
                }
            }
        }
    }
}
