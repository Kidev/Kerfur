import Qt.labs.synchronizer
import QtMultimedia
import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Tools
import Vision
import Hearing
import Skills
import qml 1.0

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
    readonly property real eyeMovementRange: KSettings.eyeMovementRange
    readonly property alias faceTracker: faceTracker
    readonly property alias speechRecognizer: speechRecognizer

    // Face tracking properties
    readonly property bool faceTrackingActive: faceTracker.enabled && faceTracker.faceDetected
    property bool isBlinking: false
    property bool isDoubleBlink: false
    property bool isTouched: false
    property bool isWinking: false
    readonly property point leftEyeCenter: Qt.point(32, 22)
    readonly property real leftOffset: ((root.targetLeftRightAngle - 90) / 90) * root.maxPupilMovement
    readonly property point leftPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool leftPupilVisible: true
    property bool maintainEyePosition: faceTracker.enabled && !faceTracker.faceDetected
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
    readonly property real rightOffset: ((root.targetUpDownAngle - 90) / 90) * root.maxPupilMovement
    readonly property point rightPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool rightPupilVisible: true

    // Properties for synchronization
    property real targetLeftRightAngle: 90
    property real targetUpDownAngle: 90
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
            blinkTimer.interval = Math.floor(Math.random() * (root.maxBlinkInterval - root.minBlinkInterval)) + root.minBlinkInterval;
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

                    if (!root.isDoubleBlink && Math.random() < root.doubleBlinkChance && !root.isTouched && !root.isWinking) {
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
        root.targetLeftRightAngle = KSettings.leftRightAngle;
        root.targetUpDownAngle = KSettings.upDownAngle;
        root.resetBlinkTimer();
    }

    // Synchronize eye angles with settings
    Synchronizer {
        id: leftRightSync

        sourceObject: root
        sourceProperty: "targetLeftRightAngle"
        targetObject: KSettings
        targetProperty: "leftRightAngle"

        onValueBounced: (object, prop) => {
            console.log("LeftRight value bounced for", prop);
        }
    }

    Synchronizer {
        id: upDownSync

        sourceObject: root
        sourceProperty: "targetUpDownAngle"
        targetObject: KSettings
        targetProperty: "upDownAngle"

        onValueBounced: (object, prop) => {
            console.log("UpDown value bounced for", prop);
        }
    }

    SoundEffect {
        id: dynamicSound

        volume: root.meowVolume
    }

    SpeechRecognizer {
        id: speechRecognizer

        enabled: KSettings.speechRecognitionEnabled // Add this setting to KSettings.qml

        onKerfurHeard: {
            // Trigger meow when "kerfur" is heard
            if (!root.isTouched && !root.isWinking && !root.isBlinking) {
                root.isTouched = true;
                root.playMeowSound();
            }
        }

        onSentenceRecognized: (text, confidence) => {
            console.log("Heard:", text, "with confidence:", confidence);
        }

        // Everything heard goes to the skills, whoever said it. What a rank
        // decides is not whether somebody is listened to but whether what they
        // said is allowed to act, and that decision belongs to the trigger
        // rather than to this connection.
        onUtteranceReady: (text, confidence, speaker, priority, authorised) => {
            Skills.hear(text, speaker, priority, authorised);
        }
    }

    FaceTracker {
        id: faceTracker

        enabled: KSettings.faceTrackingEnabled

        onEnabledChanged: {
            if (!enabled) {
                // Return to center when face tracking is disabled
                centerReturnTimer.start();
            }
        }
        onErrorStringChanged: {
            if (errorString !== "") {
                console.warn("FaceTracker error:", errorString);
            }
        }
        onFaceCenterChanged: {
            if (faceTracker.faceDetected && !root.isBlinking && !root.isTouched && !root.isWinking && faceTracker.enabled) {

                // Calculate relative movement from center (0.5, 0.5)
                const faceRelativeX = faceCenter.x - 0.5; // Range: -0.5 to 0.5
                const faceRelativeY = faceCenter.y - 0.5; // Range: -0.5 to 0.5

                // Apply eye movement range multiplier
                const eyeMovementX = faceRelativeX * root.eyeMovementRange;
                const eyeMovementY = faceRelativeY * root.eyeMovementRange;

                // Convert to angle range (90 degrees is center, 0-180 is full range)
                const centerAngle = 90;
                const maxAngleRange = 90; // 90 degrees in each direction from center

                const newLeftRight = centerAngle + (eyeMovementX * maxAngleRange);
                const newUpDown = centerAngle + (eyeMovementY * maxAngleRange);

                // Clamp to valid range (0-180)
                const clampedLeftRight = Math.max(0, Math.min(180, newLeftRight));
                const clampedUpDown = Math.max(0, Math.min(180, newUpDown));

                // Apply additional smoothing for more natural movement
                const smoothingFactor = 0.15; // Additional smoothing on top of tracker smoothing

                root.targetLeftRightAngle = root.targetLeftRightAngle + (clampedLeftRight - root.targetLeftRightAngle) * smoothingFactor;
                root.targetUpDownAngle = root.targetUpDownAngle + (clampedUpDown - root.targetUpDownAngle) * smoothingFactor;
            }
        }
        onFaceDetectedChanged: {
            if (!faceDetected && root.faceTrackingActive) {
                // Face lost but tracking still active - do NOT return to center
                centerReturnTimer.stop();
            } else if (!faceDetected && !faceTracker.enabled) {
                // Face tracking disabled - gradually return to center
                centerReturnTimer.start();
            } else {
                // Face detected - stop any center return
                centerReturnTimer.stop();
            }
        }
    }

    // Timer to handle face tracking loss - keeps eyes at last position instead of centering
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
            // Only return to center if face tracking is disabled
            if (!faceTracker.enabled && steps < maxSteps) {
                const centerAngle = 90;
                const currentLeftRight = root.targetLeftRightAngle;
                const currentUpDown = root.targetUpDownAngle;

                const returnSpeed = 0.1;
                root.targetLeftRightAngle = currentLeftRight + (centerAngle - currentLeftRight) * returnSpeed;
                root.targetUpDownAngle = currentUpDown + (centerAngle - currentUpDown) * returnSpeed;

                steps++;

                // Stop when close enough to center
                if (Math.abs(root.targetLeftRightAngle - centerAngle) < 1 && Math.abs(root.targetUpDownAngle - centerAngle) < 1) {
                    stop();
                    steps = 0;
                }
            } else {
                // Face tracking is still enabled, just stop the timer and maintain position
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
        height: Math.min(root.height, root.width * (displayImage.sourceSize.height / displayImage.sourceSize.width))
        width: Math.min(root.width, root.height * (displayImage.sourceSize.width / displayImage.sourceSize.height))

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
            y: displayContainer.relativeScaleHeight * (root.leftEyeCenter.y + root.leftPupilOffset.y)

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
            x: displayContainer.relativeScaleWidth * (root.rightEyeCenter.x + root.rightPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.rightEyeCenter.y + root.rightPupilOffset.y)

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
        autoPaddingEnabled: KSettings.autoPaddingEnabled
        layer.enabled: true
        maskEnabled: !root.isBlinking && !root.isTouched
        maskInverted: true
        maskSpreadAtMax: KSettings.maskSpreadAtMax
        maskSpreadAtMin: KSettings.maskSpreadAtMin
        maskThresholdMax: KSettings.maskThresholdMax
        maskThresholdMin: KSettings.maskThresholdMin

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

            if (!root.isTouched && !root.isWinking && root.allowsBlinks && root.state === "normal") {
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

        property bool maintainPosition: faceTracker.enabled && !faceTracker.faceDetected
        property bool shouldTrack: !root.isBlinking && !root.isTouched && (!pupilTrackingArea.pressed || root.isWinking)
        property bool useFaceTracking: faceTracker.enabled && faceTracker.faceDetected

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        cursorShape: KSettings.showControls ? Qt.ArrowCursor : Qt.BlankCursor
        hoverEnabled: !useFaceTracking && !maintainPosition
        propagateComposedEvents: false

        onMouseXChanged: {
            if (!useFaceTracking && !maintainPosition && pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                root.targetLeftRightAngle = (pupilTrackingArea.mouseX / pupilTrackingArea.width) * 180;
            }
        }
        onMouseYChanged: {
            if (!useFaceTracking && !maintainPosition && pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                root.targetUpDownAngle = (pupilTrackingArea.mouseY / pupilTrackingArea.height) * 180;
            }
        }
        onPressed: mouse => {
            if (mouse.button === Qt.LeftButton) {
                root.isTouched = true;
                mouse.accepted = true;
            } else if (mouse.button === Qt.RightButton) {
                var winkSide = Math.random() < 0.5 ? Face.WinkType.Left : Face.WinkType.Right;
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
