import QtQuick
import QtQuick.Window
import QtMultimedia
import QtQuick.Effects
import Tools
import Vision

Item {
    id: root

    enum WinkType {
        None,
        Left,
        Right
    }

    property bool allowsBlinks: true
    readonly property int blinkDuration: 150
    property string currentEyesImage: root.pathEyesOpened
    readonly property real doubleBlinkChance: 0.1
    readonly property int doubleBlinkDuration: 100
    readonly property alias faceTracker: faceTracker

    // Face tracking properties
    readonly property bool faceTrackingActive: faceTracker.enabled && faceTracker.faceDetected
    readonly property real faceTrackingInfluence: root.settings.faceTrackingSensitivity
    property bool isBlinking: false
    property bool isDoubleBlink: false
    property bool isTouched: false
    property bool isWinking: false
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
    property int winkType: Face.WinkType.None

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
        if (!root.isTouched && !root.isWinking && root.allowsBlinks) {
            root.isBlinking = true;
        }
    }

    function startDoubleBlink() {
        if (!root.isTouched && !root.isWinking && root.allowsBlinks) {
            root.isDoubleBlink = true;
            root.isBlinking = true;
        }
    }

    function startWinking(winkSide) {
        root.winkType = winkSide;
        root.isWinking = true;
        root.cancelDoubleBlink();
    }

    function stopBlinking() {
        root.isBlinking = false;
    }

    function stopWinking() {
        root.isWinking = false;
        root.winkType = Face.WinkType.None;
    }

    function updatePupilPositions(leftRight, upDown) {
        // Smooth the transitions when face tracking is active
        if (root.faceTrackingActive) {
            root.settings.leftRightAngle = Math.max(0, Math.min(180, leftRight));
            root.settings.upDownAngle = Math.max(0, Math.min(180, upDown));
        } else {
            // Immediate update for manual control
            root.settings.leftRightAngle = Math.max(0, Math.min(180, leftRight));
            root.settings.upDownAngle = Math.max(0, Math.min(180, upDown));
        }
    }

    state: "normal"
    visible: true

    states: [
        State {
            name: "normal"
            when: !root.isTouched && !root.isBlinking && !root.isWinking

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
            when: root.isBlinking && !root.isTouched && !root.isWinking

            PropertyChanges {
                root.currentEyesImage: root.pathEyesClosed
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "wink_left"
            when: root.isWinking && root.winkType === Face.WinkType.Left

            PropertyChanges {
                root.currentEyesImage: root.pathEyesLeftWink
                root.leftPupilVisible: false
                root.rightPupilVisible: true
            }
        },
        State {
            name: "wink_right"
            when: root.isWinking && root.winkType === Face.WinkType.Right

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
            from: "blink"
            to: "normal"

            ScriptAction {
                script: {
                    root.allowsBlinks = true;

                    if (!root.isDoubleBlink && Math.random() < root.doubleBlinkChance &&
                            !root.isTouched && !root.isWinking) {
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

    FaceTracker {
        id: faceTracker

        enabled: root.settings.faceTrackingEnabled

        onEnabledChanged: {
            console.log("FaceTracker enabled:", enabled);
        }
        onErrorStringChanged: {
            if (errorString !== "") {
                console.warn("FaceTracker error:", errorString);
            } else {
                console.log("FaceTracker: Error cleared");
            }
        }
        onFaceCenterChanged: {
            if (faceTracker.faceDetected && !root.isBlinking && !root.isTouched && !root.isWinking
                    && faceTracker.enabled) {

                // Apply sensitivity with smoothing
                const sensitivity = root.faceTrackingInfluence;

                // Convert face position (0.0-1.0) to angle (0-180)
                // Apply some smoothing to reduce jitter
                const targetLeftRight = faceCenter.x * 180 * sensitivity;
                const targetUpDown = faceCenter.y * 180 * sensitivity;

                // Clamp to valid range
                const clampedLeftRight = Math.max(0, Math.min(180, targetLeftRight));
                const clampedUpDown = Math.max(0, Math.min(180, targetUpDown));

                // Apply smoothing for more natural movement
                const currentLeftRight = root.settings.leftRightAngle;
                const currentUpDown = root.settings.upDownAngle;

                const smoothingFactor = 0.3; // Adjust for more/less smoothing
                const smoothedLeftRight = currentLeftRight + (clampedLeftRight - currentLeftRight)
                      * smoothingFactor;
                const smoothedUpDown = currentUpDown + (clampedUpDown - currentUpDown)
                      * smoothingFactor;

                root.updatePupilPositions(smoothedLeftRight, smoothedUpDown);
            }
        }
        onFaceDetectedChanged: {
            console.log("Face detected:", faceDetected);
            if (!faceDetected && root.faceTrackingActive) {
                // Gradually return to center when face is lost
                centerReturnTimer.start();
            } else {
                centerReturnTimer.stop();
            }
        }
    }

    // Timer to gradually return pupils to center when face tracking is lost
    Timer {
        id: centerReturnTimer

        readonly property int maxSteps: 20
        property int steps: 0

        interval: 100
        repeat: true
        running: false

        onRunningChanged: {
            if (running) {
                steps = 0;
            }
        }
        onTriggered: {
            if (!root.faceTrackingActive && steps < maxSteps) {
                const centerAngle = 90;
                const currentLeftRight = root.settings.leftRightAngle;
                const currentUpDown = root.settings.upDownAngle;

                const returnSpeed = 0.1;
                const newLeftRight = currentLeftRight + (centerAngle - currentLeftRight)
                      * returnSpeed;
                const newUpDown = currentUpDown + (centerAngle - currentUpDown) * returnSpeed;

                root.updatePupilPositions(newLeftRight, newUpDown);
                steps++;

                // Stop when close enough to center
                if (Math.abs(newLeftRight - centerAngle) < 1 && Math.abs(newUpDown - centerAngle)
                        < 1) {
                    stop();
                    steps = 0;
                }
            } else {
                stop();
                steps = 0;
            }
        }
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

            // Smooth transitions for face tracking
            Behavior on x {
                enabled: root.faceTrackingActive

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on y {
                enabled: root.faceTrackingActive

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }
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

            // Smooth transitions for face tracking
            Behavior on x {
                enabled: root.faceTrackingActive

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on y {
                enabled: root.faceTrackingActive

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }
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

            if (!root.isTouched && !root.isWinking && root.allowsBlinks && root.state
                    === "normal") {
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
            root.stopWinking();
        }
        onReleased: {
            root.isTouched = false;
        }
    }

    MouseArea {
        id: pupilTrackingArea

        property bool shouldTrack: !root.isBlinking && !root.isTouched && (
                                       !pupilTrackingArea.pressed || root.isWinking)
        property bool useFaceTracking: faceTracker.enabled && faceTracker.faceDetected

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        cursorShape: root.settings.showControls ? Qt.ArrowCursor : Qt.BlankCursor
        hoverEnabled: !useFaceTracking  // Disable mouse hover when using face tracking
        propagateComposedEvents: false

        onMouseXChanged: {
            if (!useFaceTracking && pupilTrackingArea.shouldTrack
                    && pupilTrackingArea.containsMouse) {
                root.settings.leftRightAngle = (pupilTrackingArea.mouseX / pupilTrackingArea.width)
                        * 180;
            }
        }
        onMouseYChanged: {
            if (!useFaceTracking && pupilTrackingArea.shouldTrack
                    && pupilTrackingArea.containsMouse) {
                root.settings.upDownAngle = (pupilTrackingArea.mouseY / pupilTrackingArea.height)
                        * 180;
            }
        }
        onPressed: mouse => {
                       if (mouse.button === Qt.LeftButton) {
                           root.isTouched = true;
                           mouse.accepted = true;
                       } else if (mouse.button === Qt.RightButton) {
                           var winkSide = Math.random() < 0.5 ? Face.WinkType.Left :
                                                                Face.WinkType.Right;
                           root.startWinking(winkSide);
                           mouse.accepted = true;
                       }
                   }
        onReleased: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            root.isTouched = false;
                            mouse.accepted = true;
                        } else if (mouse.button === Qt.RightButton) {
                            root.stopWinking();
                            mouse.accepted = true;
                        }
                    }
    }

    // Face tracking status indicator
    Rectangle {
        anchors.margins: 10
        anchors.right: parent.right
        anchors.top: parent.top
        color: root.faceTrackingActive ? "lime" : (faceTracker.enabled ? "orange" : "gray")
        height: 20
        opacity: 0.7
        radius: 10
        visible: faceTracker.enabled
        width: 20

        SequentialAnimation on opacity {
            loops: Animation.Infinite
            running: faceTracker.enabled && !root.faceTrackingActive

            NumberAnimation {
                duration: 500
                to: 0.3
            }

            NumberAnimation {
                duration: 500
                to: 0.7
            }
        }
    }
}
