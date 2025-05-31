import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Effects
import Tools

Item {
    id: root

    property bool allowsBlinks: true
    readonly property int blinkDuration: 150
    property string currentEyesImage: root.pathEyesOpened
    readonly property real doubleBlinkChance: 0.1
    readonly property int doubleBlinkDuration: 100
    property bool isBlinking: false
    property bool isDoubleBlink: false
    property bool isTouched: false
    readonly property point leftEyeCenter: Qt.point(32, 22)
    readonly property real leftOffset: ((root.settings.leftRightAngle - 90) / 90)
                                       * root.maxPupilMovement

    readonly property point leftPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool leftPupilVisible: true
    readonly property int maxBlinkInterval: 8000
    readonly property int maxPupilMovement: 5
    readonly property real meowVolume: 1
    readonly property int minBlinkInterval: 4000
    readonly property string pathEyesClosed: "/assets/img/eyes_closed.png"
    readonly property string pathEyesLeftWink: "/assets/img/eyes_left_wink.png"
    readonly property string pathEyesMeow: "/assets/img/eyes_meow.png"
    readonly property string pathEyesOpened: "/assets/img/eyes_opened.png"
    readonly property string pathEyesPupil: "/assets/img/pupil.png"
    readonly property string pathEyesRightWink: "/assets/img/eyes_right_wink.png"
    readonly property string pathSoundMeow: "/assets/sound/meow.wav"
    property bool pendingDoubleBlink: false
    readonly property point rightEyeCenter: Qt.point(90, 22)
    readonly property real rightOffset: ((root.settings.upDownAngle - 90) / 90)
                                        * root.maxPupilMovement

    readonly property point rightPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool rightPupilVisible: true
    required property KSettings settings

    function cancelDoubleBlink() {
        root.pendingDoubleBlink = false;
        root.isDoubleBlink = false;
        doubleBlinkTimer.stop();
    }

    function playMeowSound() {
        dynamicSound.source = root.pathSoundMeow;
        dynamicSound.play();
    }

    function resetBlinkTimer() {
        if (root.allowsBlinks && !root.pendingDoubleBlink) {
            blinkTimer.interval = Math.floor(Math.random() * (root.maxBlinkInterval
                                                              - root.minBlinkInterval))
                    + root.minBlinkInterval;
            blinkTimer.restart();
        }
    }

    function startBlinking() {
        if (!root.isTouched && root.allowsBlinks) {
            root.isBlinking = true;
        }
    }

    function startDoubleBlink() {
        if (!root.isTouched && root.allowsBlinks) {
            root.isDoubleBlink = true;
            root.isBlinking = true;
        }
    }

    function stopBlinking() {
        root.isBlinking = false;
    }

    function updatePupilPositions(leftRight, upDown) {
        root.settings.leftRightAngle = Math.max(0, Math.min(180, leftRight));
        root.settings.upDownAngle = Math.max(0, Math.min(180, upDown));
    }

    state: "normal"
    visible: true

    states: [
        State {
            name: "normal"
            when: !root.isTouched && !root.isBlinking

            PropertyChanges {
                root.currentEyesImage: root.pathEyesOpened
                root.leftPupilVisible: true
                root.rightPupilVisible: true
            }
        },
        State {
            name: "meow"
            when: root.isTouched

            PropertyChanges {
                root.currentEyesImage: root.pathEyesMeow
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "blink"
            when: root.isBlinking && !root.isTouched

            PropertyChanges {
                root.currentEyesImage: root.pathEyesClosed
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "wink_left"

            PropertyChanges {
                root.currentEyesImage: root.pathEyesLeftWink
                root.leftPupilVisible: false
                root.rightPupilVisible: true
            }
        },
        State {
            name: "wink_right"

            PropertyChanges {
                root.currentEyesImage: root.pathEyesRightWink
                root.leftPupilVisible: true
                root.rightPupilVisible: false
            }
        }
    ]
    transitions: [
        Transition {
            to: "meow"

            ScriptAction {
                script: {
                    blinkTimer.stop();
                    root.cancelDoubleBlink();
                    root.allowsBlinks = false;
                    dynamicSound.source = root.pathSoundMeow;
                    dynamicSound.play();
                }
            }
        },
        Transition {
            to: "normal"

            ScriptAction {
                script: {
                    root.isBlinking = false;
                    root.allowsBlinks = true;

                    if (!root.pendingDoubleBlink) {
                        root.resetBlinkTimer();
                    }
                }
            }
        },
        Transition {
            to: "blink"

            ScriptAction {
                script: {
                    root.allowsBlinks = false;
                    blinkDurationTimer.start();
                }
            }
        },
        Transition {
            to: "wink_left"

            ScriptAction {
                script: {
                    blinkTimer.stop();
                    root.cancelDoubleBlink();
                    root.allowsBlinks = false;
                }
            }
        },
        Transition {
            to: "wink_right"

            ScriptAction {
                script: {
                    blinkTimer.stop();
                    root.cancelDoubleBlink();
                    root.allowsBlinks = false;
                }
            }
        },
        Transition {
            from: "blink"
            to: "normal"

            ScriptAction {
                script: {
                    root.allowsBlinks = true;

                    if (!root.isDoubleBlink && Math.random() < root.doubleBlinkChance &&
                            !root.isTouched) {
                        root.pendingDoubleBlink = true;
                        doubleBlinkTimer.start();
                    } else {
                        root.resetBlinkTimer();
                    }

                    root.isDoubleBlink = false;
                }
            }
        }
    ]

    Component.onCompleted: {
        root.updatePupilPositions(root.settings.leftRightAngle, root.settings.upDownAngle);
        root.resetBlinkTimer();
    }

    SoundEffect {
        id: dynamicSound

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
            source: root.currentEyesImage

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
            visible: root.leftPupilVisible
            width: leftPupil.sourceSize.width * displayContainer.relativeScaleWidth
            x: displayContainer.relativeScaleWidth * (root.leftEyeCenter.x + root.leftPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.leftEyeCenter.y
                                                       + root.leftPupilOffset.y)
        }

        Image {
            id: rightPupil

            height: rightPupil.sourceSize.height * displayContainer.relativeScaleHeight
            source: root.pathEyesPupil
            visible: root.rightPupilVisible
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
        autoPaddingEnabled: root.settings.autoPaddingEnabled
        layer.enabled: true
        maskEnabled: !root.isBlinking && !root.isTouched
        maskInverted: true
        maskSpreadAtMax: root.settings.maskSpreadAtMax
        maskSpreadAtMin: root.settings.maskSpreadAtMin
        maskThresholdMax: root.settings.maskThresholdMax
        maskThresholdMin: root.settings.maskThresholdMin

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
        settings: root.settings
        source: maskedDisplayImage
    }

    Timer {
        id: blinkTimer

        interval: root.minBlinkInterval
        repeat: true
        running: false

        onTriggered: root.startBlinking()
    }

    Timer {
        id: blinkDurationTimer

        interval: root.blinkDuration
        repeat: false
        running: false

        onTriggered: {
            root.stopBlinking();
        }
    }

    Timer {
        id: doubleBlinkTimer

        interval: root.doubleBlinkDuration
        repeat: false
        running: false

        onTriggered: {
            root.pendingDoubleBlink = false;

            if (!root.isTouched && root.allowsBlinks && root.state === "normal") {
                root.startDoubleBlink();
            } else {
                root.resetBlinkTimer();
            }
        }
    }

    MultiPointTouchArea {
        id: touchArea

        anchors.fill: parent

        onPressed: {
            root.isTouched = true;
            root.cancelDoubleBlink();
        }
        onReleased: {
            root.isTouched = false;
        }
    }

    MouseArea {
        id: pupilTrackingArea

        property bool shouldTrack: !root.isBlinking && !root.isTouched && !pupilTrackingArea.pressed

        anchors.fill: parent
        cursorShape: root.settings.showControls ? Qt.ArrowCursor : Qt.BlankCursor
        hoverEnabled: true
        propagateComposedEvents: true

        onMouseXChanged: {
            if (pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                root.settings.leftRightAngle = (pupilTrackingArea.mouseX / pupilTrackingArea.width)
                        * 180;
            }
        }
        onMouseYChanged: {
            if (pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                root.settings.upDownAngle = (pupilTrackingArea.mouseY / pupilTrackingArea.height)
                        * 180;
            }
        }
        onPressed: mouse => mouse.accepted = false
        onReleased: mouse => mouse.accepted = false
    }
}
