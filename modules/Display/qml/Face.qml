import Core
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
    readonly property bool faceTrackingActive: faceTracker.enabled && faceTracker.following
    // How long a press on the face lasts before it counts as `held`.
    readonly property int holdDuration: 3000
    property bool isBlinking: false
    property bool isDoubleBlink: false
    // A meow that was not a touch: the name heard, or somebody greeted. It
    // lasts `meowDuration` and ends by itself.
    property bool isMeowing: false
    // Asleep while the models are stopped and only motion is looked for.
    readonly property bool isSleeping: faceTracker.enabled && faceTracker.watching
    property bool isTouched: false
    property bool isWinking: false
    readonly property point leftEyeCenter: Qt.point(32, 22)
    readonly property real leftOffset: ((root.shownLeftRightAngle - 90) / 90) * root.maxPupilMovement
    readonly property point leftPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool leftPupilVisible: true
    // How long the eyes stay on the side where somebody was last seen before
    // they look straight ahead again.
    readonly property int lookAheadDelay: Math.round(KSettings.lookAheadAfterSeconds * 1000)
    property bool maintainEyePosition: faceTracker.enabled && !faceTracker.following
    readonly property int maxBlinkInterval: 8000
    readonly property int maxPupilMovement: 5
    // How quickly the drawn pupils catch up with the target angles, in
    // seconds: the time to cover a little more than half the way. A fraction
    // of the gap between two analysis passes, so a pupil has nearly arrived
    // when the next position comes in, and it keeps its speed through that
    // update instead of stopping on each one.
    readonly property real pupilSmoothTime: Math.max(0.03, Math.min(0.08, 0.4 / Math.max(1, faceTracker.updateHz)))
    // A little longer than the sound, so the face is still meowing when it
    // stops.
    readonly property int meowDuration: 900
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
    readonly property real rightOffset: ((root.shownUpDownAngle - 90) / 90) * root.maxPupilMovement
    readonly property point rightPupilOffset: Qt.point(root.leftOffset, root.rightOffset)
    property bool rightPupilVisible: true

    // Where the pupils are drawn, chasing the target angles below.
    property real shownLeftRightAngle: 90
    property real shownUpDownAngle: 90
    property real shownLeftRightVelocity: 0
    property real shownUpDownVelocity: 0

    // Properties for synchronization
    property real targetLeftRightAngle: 90
    property real targetUpDownAngle: 90
    property int winkType: Face.WinkType.None

    // A press, touch or left button, kept on the face for `holdDuration`. The
    // way to reach the control panel on a screen with no keyboard.
    signal held

    // One step of a critically damped spring from `current` towards `target`,
    // `dt` seconds long. Returns the new position and velocity. The velocity
    // carries over from one target to the next, which is what turns a few
    // positions a second into one continuous movement, and the time to arrive
    // is the same for a long move as for a short one, so smoothing never slows
    // a jump to somebody else.
    function chase(current, velocity, target, dt) {
        const omega = 2 / root.pupilSmoothTime;
        const x = omega * dt;
        const decay = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
        const offset = current - target;
        const pull = (velocity + omega * offset) * dt;
        return [target + (offset + pull) * decay, (velocity - omega * pull) * decay];
    }

    function cancelDoubleBlink() {
        root.pendingDoubleBlink = false;
        root.isDoubleBlink = false;
        doubleBlinkTimer.stop();
    }

    // Everybody the gallery names in this pass, by face or by body.
    function identitiesInView() {
        const seen = [];
        for (const face of faceTracker.faces) {
            if (face.known) {
                seen.push(face.identity);
            }
        }
        for (const person of faceTracker.people) {
            if (person.known && person.identity) {
                seen.push(person.identity);
            }
        }
        return seen;
    }

    // Straight ahead, as if somebody had just stepped in front: the pupils
    // chase the centre the way they chase a person.
    function lookAhead() {
        root.targetLeftRightAngle = 90;
        root.targetUpDownAngle = 90;
    }

    // The same face and sound as a touch, for `meowDuration`. A touch already
    // meowing keeps the face until it is released.
    function meow() {
        if (root.isMeowing || root.isTouched) {
            return;
        }
        root.stopWinking();
        root.cancelDoubleBlink();
        root.isMeowing = true;
        meowTimer.restart();
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
            when: !root.isTouched && !root.isMeowing && !root.isBlinking && !root.isWinking && !root.isSleeping

            PropertyChanges {
                root.currentEyesImage: root.pathEyesOpened
                root.leftPupilVisible: true
                root.rightPupilVisible: true
            }
        },
        State {
            name: "meow"
            when: root.isTouched || root.isMeowing

            PropertyChanges {
                root.currentEyesImage: root.pathEyesMeow
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "blink"
            when: root.isBlinking && !root.isTouched && !root.isMeowing && !root.isWinking && !root.isSleeping

            PropertyChanges {
                root.currentEyesImage: root.pathEyesClosed
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "sleep"
            when: root.isSleeping && !root.isTouched && !root.isMeowing && !root.isWinking

            PropertyChanges {
                root.currentEyesImage: root.pathEyesClosed
                root.leftPupilVisible: false
                root.rightPupilVisible: false
            }
        },
        State {
            name: "wink_left"
            when: root.isWinking && !root.isTouched && !root.isMeowing && root.winkType === Face.WinkType.Left

            PropertyChanges {
                root.currentEyesImage: root.pathEyesLeftWink
                root.leftPupilVisible: false
                root.rightPupilVisible: true
            }
        },
        State {
            name: "wink_right"
            when: root.isWinking && !root.isTouched && !root.isMeowing && root.winkType === Face.WinkType.Right

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
            to: "sleep"

            ScriptAction {
                script: {
                    blinkTimer.stop();
                    root.cancelDoubleBlink();
                    root.isBlinking = false;
                    root.allowsBlinks = false;
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
        root.shownLeftRightAngle = root.targetLeftRightAngle;
        root.shownUpDownAngle = root.targetUpDownAngle;
        root.resetBlinkTimer();
    }

    // Runs only while the pupils are away from their target, once per frame
    // drawn, so their movement is as smooth as the display whatever the
    // analysis rate is.
    FrameAnimation {
        id: pupilChase

        // Close enough to stop, in degrees and degrees per second. A tenth of
        // a degree is far below a pixel of pupil travel.
        readonly property real settled: 0.1

        running: root.shownLeftRightAngle !== root.targetLeftRightAngle || root.shownUpDownAngle !== root.targetUpDownAngle

        onRunningChanged: {
            if (!running) {
                root.shownLeftRightVelocity = 0;
                root.shownUpDownVelocity = 0;
            }
        }
        onTriggered: {
            // A stalled frame is not a reason to jump.
            const dt = Math.min(pupilChase.frameTime, 0.05);
            const leftRight = root.chase(root.shownLeftRightAngle, root.shownLeftRightVelocity, root.targetLeftRightAngle, dt);
            const upDown = root.chase(root.shownUpDownAngle, root.shownUpDownVelocity, root.targetUpDownAngle, dt);
            const done = Math.abs(leftRight[0] - root.targetLeftRightAngle) < pupilChase.settled && Math.abs(upDown[0] - root.targetUpDownAngle) < pupilChase.settled && Math.abs(leftRight[1]) < pupilChase.settled && Math.abs(upDown[1]) < pupilChase.settled;
            if (done) {
                root.shownLeftRightAngle = root.targetLeftRightAngle;
                root.shownUpDownAngle = root.targetUpDownAngle;
                return;
            }
            root.shownLeftRightVelocity = leftRight[1];
            root.shownUpDownVelocity = upDown[1];
            root.shownLeftRightAngle = leftRight[0];
            root.shownUpDownAngle = upDown[0];
        }
    }

    // Synchronize eye angles with settings
    PropertySynchronizer {
        id: leftRightSync

        sourceObject: root
        sourceProperty: "targetLeftRightAngle"
        targetObject: KSettings
        targetProperty: "leftRightAngle"

        onValueBounced: (object, prop) => {
            console.log("LeftRight value bounced for", prop);
        }
    }

    PropertySynchronizer {
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

        onKerfurHeard: root.meow()

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

        analysisMaxSide: KSettings.analysisMaxSide
        cameraHeight: KSettings.cameraHeight
        cameraId: KSettings.cameraId
        cameraTilt: KSettings.cameraTilt
        detectionConfidence: KSettings.faceConfidence
        enabled: KSettings.faceTrackingEnabled
        expressionEnabled: KSettings.expressionEnabled
        fieldOfView: KSettings.fieldOfView
        handConfidence: KSettings.handConfidence
        idleAfterSeconds: KSettings.idleAfterSeconds
        idleWatch: KSettings.idleWatch
        identityThreshold: KSettings.identityThreshold
        keypointConfidence: KSettings.keypointConfidence
        loneStrangerHands: KSettings.loneStrangerHands
        objectConfidence: KSettings.objectConfidence
        palmConfidence: KSettings.palmConfidence
        personConfidence: KSettings.personConfidence
        previewMaxSide: KSettings.previewMaxSide
        probeSeconds: KSettings.probeSeconds
        requestedResolution: KSettings.captureResolution
        screenOffset: KSettings.screenOffset
        trackingSmoothing: KSettings.trackingSmoothing
        updateHz: KSettings.visionUpdateHz
        wakePercent: KSettings.wakePercent
        watchHz: KSettings.watchHz

        onEnabledChanged: {
            if (!enabled) {
                // Return to center when face tracking is disabled
                lookAheadTimer.stop();
                centerReturnTimer.start();
            }
        }
        onFacesChanged: greeter.see(root.identitiesInView())
        onErrorStringChanged: {
            if (errorString !== "") {
                console.warn("FaceTracker error:", errorString);
            }
        }
        // The tracker gives the turn from the centre of the screen to the
        // person followed, in degrees, with its jitter filtered. 90 is
        // straight ahead on both axes and the range scales the turn, so at 1
        // the pupils turn exactly as far as the person is off axis, and reach
        // the rim at somebody 90 degrees to the side. `pupilChase` moves the
        // drawn pupils there.
        onGazeChanged: {
            if (faceTracker.following && !root.isBlinking && !root.isTouched && !root.isMeowing && !root.isWinking && faceTracker.enabled) {
                root.targetLeftRightAngle = Math.max(0, Math.min(180, 90 + faceTracker.gazeYaw * root.eyeMovementRange));
                root.targetUpDownAngle = Math.max(0, Math.min(180, 90 + faceTracker.gazePitch * root.eyeMovementRange));
            }
        }
        // Somebody lost is looked for where they were last seen, for
        // `lookAheadDelay`, and then the eyes look straight ahead.
        onFollowingChanged: {
            if (following) {
                lookAheadTimer.stop();
                centerReturnTimer.stop();
            } else if (faceTracker.enabled) {
                centerReturnTimer.stop();
                lookAheadTimer.restart();
            } else {
                lookAheadTimer.stop();
                centerReturnTimer.start();
            }
        }
    }

    Timer {
        id: lookAheadTimer

        interval: root.lookAheadDelay
        repeat: false

        onTriggered: {
            if (faceTracker.enabled && !faceTracker.following) {
                root.lookAhead();
            }
        }
    }

    // Greets people the gallery knows when they come back after a long time
    // away. Time asleep counts several times over.
    Greeter {
        id: greeter

        asleep: root.isSleeping
        enabled: KSettings.greetingEnabled
        greetAfterHours: KSettings.greetAfterHours
        sleepFactor: KSettings.sleepTimeFactor

        onGreeted: root.meow()
    }

    Timer {
        id: meowTimer

        interval: root.meowDuration
        repeat: false

        onTriggered: root.isMeowing = false
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

        // The LED screen decides where the picture goes, so its pixels land
        // on the screen's cells.
        height: ledScreen.pictureRect.height
        width: ledScreen.pictureRect.width
        x: ledScreen.pictureRect.x
        y: ledScreen.pictureRect.y

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
        }

        Image {
            id: rightPupil

            height: rightPupil.sourceSize.height * displayContainer.relativeScaleHeight
            source: root.pathEyesPupil
            visible: root.rightPupilVisible
            width: rightPupil.sourceSize.width * displayContainer.relativeScaleWidth
            x: displayContainer.relativeScaleWidth * (root.rightEyeCenter.x + root.rightPupilOffset.x)
            y: displayContainer.relativeScaleHeight * (root.rightEyeCenter.y + root.rightPupilOffset.y)
        }
    }

    // What the LED screen lights: the eyes, and the marks of a sleeping face
    // beside them. Laid over the eye picture, in its coordinates.
    Item {
        id: faceLayer

        anchors.fill: displayContainer

        MultiEffect {
            id: maskedDisplayImage

            anchors.fill: parent
            autoPaddingEnabled: KSettings.autoPaddingEnabled
            // No layer: the LED screen captures this item with hideSource, and a
            // layered item stays drawn underneath the capture.
            maskEnabled: !root.isBlinking && !root.isTouched && !root.isMeowing && !root.isSleeping
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

        // Three z's rising from above the right eye into the empty corner of
        // the picture, each growing and fading as it goes, a third of a cycle
        // apart.
        Item {
            id: sleepMarks

            // Where each mark starts and ends, and its size there, in pixels of
            // the eye picture (147 by 70).
            readonly property point rise: Qt.point(110, 27)
            readonly property point peak: Qt.point(140, 2)
            readonly property real smallest: 4
            readonly property real largest: 9
            readonly property real stroke: 1.6
            readonly property int count: 3
            // Milliseconds for one mark to rise all the way.
            readonly property int period: 4200
            property real cycle: 0

            anchors.fill: parent
            opacity: root.state === "sleep" ? 1 : 0
            visible: sleepMarks.opacity > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 400
                }
            }

            // Fifteen steps a second. A step moves a mark by less than one LED
            // cell, and a face that is asleep has no reason to redraw the
            // screen at the display rate.
            Timer {
                id: sleepMarkStep

                interval: 66
                repeat: true
                running: sleepMarks.visible

                onTriggered: sleepMarks.cycle = (sleepMarks.cycle + sleepMarkStep.interval / sleepMarks.period) % 1
            }

            Repeater {
                model: sleepMarks.count

                // A z the size of the largest mark, scaled down, so its
                // geometry is built once and only moved.
                Item {
                    id: mark

                    required property int index

                    // How far this mark is along its rise, from 0 to 1.
                    readonly property real progress: (sleepMarks.cycle + mark.index / sleepMarks.count) % 1
                    readonly property real side: sleepMarks.largest * displayContainer.relativeScaleWidth
                    readonly property real thickness: sleepMarks.stroke * displayContainer.relativeScaleWidth

                    height: mark.side
                    opacity: Math.sin(Math.PI * mark.progress)
                    scale: (sleepMarks.smallest + (sleepMarks.largest - sleepMarks.smallest) * mark.progress) / sleepMarks.largest
                    width: mark.side
                    // A slight sway, so the marks drift rather than slide.
                    x: displayContainer.relativeScaleWidth * (sleepMarks.rise.x + (sleepMarks.peak.x - sleepMarks.rise.x) * mark.progress + 1.5 * Math.sin(2 * Math.PI * mark.progress)) - mark.side / 2
                    y: displayContainer.relativeScaleHeight * (sleepMarks.rise.y + (sleepMarks.peak.y - sleepMarks.rise.y) * mark.progress) - mark.side / 2

                    Rectangle {
                        color: "white"
                        height: mark.thickness
                        radius: mark.thickness / 2
                        width: mark.side
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        color: "white"
                        height: mark.thickness
                        radius: mark.thickness / 2
                        rotation: -45
                        width: Math.SQRT2 * (mark.side - mark.thickness) + mark.thickness
                    }

                    Rectangle {
                        color: "white"
                        height: mark.thickness
                        radius: mark.thickness / 2
                        width: mark.side
                        y: mark.side - mark.thickness
                    }
                }
            }
        }
    }

    LedScreen {
        id: ledScreen

        anchors.fill: parent
        source: faceLayer
        sourceSize: displayImage.sourceSize
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

    Timer {
        id: holdTimer

        interval: root.holdDuration
        repeat: false

        onTriggered: root.held()
    }

    MultiPointTouchArea {
        id: touchArea

        anchors.fill: parent

        onCanceled: holdTimer.stop()
        onPressed: {
            root.isTouched = true;
            root.cancelDoubleBlink();
            root.stopWinking();
            holdTimer.restart();
        }
        onReleased: {
            root.isTouched = false;
            holdTimer.stop();
        }
    }

    MouseArea {
        id: pupilTrackingArea

        property bool maintainPosition: faceTracker.enabled && !faceTracker.following
        property bool shouldTrack: !root.isBlinking && !root.isTouched && !root.isMeowing && !root.isSleeping && (!pupilTrackingArea.pressed || root.isWinking)
        property bool useFaceTracking: faceTracker.enabled && faceTracker.following

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        cursorShape: KSettings.showControls ? Qt.ArrowCursor : Qt.BlankCursor
        hoverEnabled: !useFaceTracking && !maintainPosition
        propagateComposedEvents: false

        onCanceled: holdTimer.stop()
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
                holdTimer.restart();
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
                holdTimer.stop();
                mouse.accepted = true;
            } else if (mouse.button === Qt.RightButton) {
                root.stopWinking();
                mouse.accepted = true;
            }
        }
    }
}
