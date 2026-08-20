// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Vision
import Hearing
import Skills
import qml 1.0

/// Everything Kerfur can be told, in one column down the right hand edge.
///
/// It is a diagnostic surface as much as a settings one, so it is organised by
/// the thing being configured rather than by the kind of control: the camera
/// preview, the numbers it produces and the knobs that change them all live in
/// the same section, because reading a knob's effect anywhere else is guesswork.
///
/// Every section is closed until asked for. There are five of them and forty
/// odd controls, and a wall of sliders is a thing to scroll past rather than a
/// thing to read. The header of a closed section carries the one line that says
/// whether the thing behind it is working, which is what makes the closed state
/// useful rather than merely tidy.
Rectangle {
    id: root

    required property FaceTracker faceTracker

    // Passed in rather than declared here. Two SpeechRecognizer instances are
    // two microphones, two whisper contexts and two transcription loops
    // competing for the same words, which is what the second one did.
    required property SpeechRecognizer speechRecognizer

    // One palette, named once. The alpha lives in the background colour rather
    // than in `opacity`, because opacity on the panel fades the text with it
    // and small dim text on a bright face is the thing this panel is worst at.
    readonly property color colorAccent: "#8ad8ff"
    readonly property color colorBackground: "#f01a1c20"
    readonly property color colorBad: "#ff6b6b"
    readonly property color colorDim: "#98a0ac"
    readonly property color colorLine: "#3c414a"
    readonly property color colorOk: "#7cff5a"
    readonly property color colorRaised: "#2c3037"
    readonly property color colorSurface: "#24272c"
    readonly property color colorText: "#eceff4"
    readonly property color colorWarn: "#ffb74d"

    // The longer side the analysis image is reduced to, as the few values worth
    // offering. A free slider would invite 1919, which costs what 1920 costs
    // and buys nothing; these are the steps where the detail actually changes.
    // Two parallel arrays rather than one of objects, so the drop down needs no
    // role names and the stored value is a number and not an index into a list
    // that a later version might reorder.
    readonly property var analysisPresetLabels: ["480 - fastest", "640 - fast", "960 - default", "1280 - detailed", "1600 - finer", "1920 - maximum"]
    readonly property var analysisPresetSides: [480, 640, 960, 1280, 1600, 1920]

    // The same ladder for the preview, which is a JPEG encode and a base64 of
    // it per bound view rather than an inference chain, so it is cheap at the
    // top of the range in a way the analysis is not.
    readonly property var previewPresetLabels: ["480 - cheapest", "640 - small", "960 - default", "1280 - sharp", "1920 - maximum"]
    readonly property var previewPresetSides: [480, 640, 960, 1280, 1920]

    // How often the camera is looked at. The whole chain runs at this rate, so
    // it is the one control here that changes what Kerfur costs to leave on
    // rather than how well it sees. Named in hertz because that is what it is,
    // and annotated because six is not obviously enough until it is said that
    // nothing being watched for happens faster than that.
    readonly property var updateRateLabels: ["3 Hz - idling", "6 Hz - default", "10 Hz - responsive", "15 Hz - maximum"]
    readonly property var updateRateValues: [3, 6, 10, 15]
    readonly property var blendModeNames: ["Additive", "Screen", "Replace", "Outer"]

    // The last utterance, while it is still worth showing over the head of the
    // person who said it. Long enough to read a sentence twice, short enough
    // that it is gone before it can be mistaken for the current one.
    readonly property int spokenHoldMs: 6000
    property bool spokenShown: false
    property string spokenBy: ""
    property string spokenText: ""

    function hearingSummary() {
        if (!root.speechRecognizer || !root.speechRecognizer.enabled)
            return "off";
        if (root.speechRecognizer.errorString !== "")
            return "error";
        if (root.speechRecognizer.processing)
            return "transcribing";
        if (root.speechRecognizer.listening)
            return "listening";
        return "starting";
    }

    // The hands of whoever the eyes are following, and what they are doing.
    // The side is the person's own, so "left" is the hand they would raise if
    // asked to raise their left, whichever side of the mirrored picture it is
    // drawn on.
    function handsSummary() {
        if (!root.faceTracker || root.faceTracker.hands.length === 0)
            return "none in view";
        const parts = [];
        for (let index = 0; index < root.faceTracker.hands.length; ++index) {
            const hand = root.faceTracker.hands[index];
            if (!hand.tracked) {
                parts.push(hand.side + " estimated");
                continue;
            }
            if (hand.gesture !== "") {
                parts.push(hand.side + " " + hand.gesture);
                continue;
            }
            parts.push(hand.side + " " + (hand.grip >= 0.4 ? "closed" : "open"));
        }
        return parts.join(", ");
    }

    function visionSummary() {
        if (!root.faceTracker || !root.faceTracker.enabled)
            return "off";
        if (root.faceTracker.errorString !== "")
            return "error";
        if (!root.faceTracker.faceDetected)
            return "searching";
        if (root.faceTracker.identity !== "")
            return root.faceTracker.identity;
        return root.faceTracker.faces.length + " face" + (root.faceTracker.faces.length === 1 ? "" : "s");
    }

    // A disclosure arrow, drawn rather than typed. A glyph would depend on
    // whichever font the host happens to ship, and this is two rectangles.
    component Chevron: Item {
        id: chevron

        property color color: "white"
        property bool open: false
        property real thickness: 1.6

        readonly property real armAngle: Math.atan2(chevron.by - chevron.ay, chevron.bx - chevron.ax) * 180 / Math.PI
        readonly property real armLength: Math.hypot(chevron.bx - chevron.ax, chevron.by - chevron.ay)
        readonly property real ax: width * 0.3
        readonly property real ay: height * 0.16
        readonly property real bx: width * 0.7
        readonly property real by: height * 0.5
        readonly property real cy: height * 0.84

        implicitHeight: 9
        implicitWidth: 9
        rotation: chevron.open ? 90 : 0

        Behavior on rotation {
            NumberAnimation {
                duration: 170
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            antialiasing: true
            color: chevron.color
            height: chevron.thickness
            radius: height / 2
            rotation: chevron.armAngle
            transformOrigin: Item.Left
            width: chevron.armLength
            x: chevron.ax
            y: chevron.ay - (height / 2)
        }

        Rectangle {
            antialiasing: true
            color: chevron.color
            height: chevron.thickness
            radius: height / 2
            rotation: 180 - chevron.armAngle
            transformOrigin: Item.Left
            width: chevron.armLength
            x: chevron.bx
            y: chevron.by - (height / 2)
        }
    }

    // One collapsible group. The content is clipped and the height animated, so
    // a section that is closing does not repaint its children at full size for
    // a frame and then vanish.
    component Section: Rectangle {
        id: section

        property string summary: ""
        property color summaryColor: root.colorDim
        property string title: ""
        property bool expanded: false

        default property alias content: body.data

        readonly property int headerHeight: 32

        border.color: section.expanded ? root.colorLine : "transparent"
        border.width: 1
        clip: true
        color: section.expanded ? root.colorSurface : (headerArea.containsMouse ? "#1e2126" : "transparent")
        height: section.headerHeight + (section.expanded ? body.implicitHeight + 10 : 0)
        radius: 5
        width: parent ? parent.width : 0

        Behavior on height {
            NumberAnimation {
                duration: 170
                easing.type: Easing.OutCubic
            }
        }

        // Assigned to `children` rather than declared as plain child objects.
        // The default property of this type is aliased to the body's contents,
        // so a bare declaration here would try to make the body a child of
        // itself. This is the one place that alias has to be worked around.
        children: [
            Item {
                id: header

                anchors.left: section.left
                anchors.right: section.right
                anchors.top: section.top
                height: section.headerHeight

                Chevron {
                    id: chevron

                    anchors.left: parent.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    color: section.expanded ? root.colorAccent : root.colorDim
                    open: section.expanded
                }

                Text {
                    id: titleText

                    anchors.left: chevron.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: section.expanded ? root.colorText : "#c6cbd3"
                    font.bold: true
                    font.pixelSize: 11
                    text: section.title
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    color: section.summaryColor
                    elide: Text.ElideRight
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignRight
                    opacity: section.expanded ? 0 : 1
                    text: section.summary
                    width: Math.max(0, parent.width - titleText.width - 42)

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }
                }

                MouseArea {
                    id: headerArea

                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked: section.expanded = !section.expanded
                }
            },
            Column {
                id: body

                anchors.left: section.left
                anchors.leftMargin: 10
                anchors.right: section.right
                anchors.rightMargin: 10
                anchors.top: header.bottom
                spacing: 8
                visible: section.height > section.headerHeight + 1
            }
        ]
    }

    // A label and an on/off switch. The whole row is the hit target: a 12 pixel
    // knob is not something to aim at on a screen being watched from across a
    // room.
    component Toggle: Item {
        id: toggle

        property bool checked: false
        property string label: ""

        signal toggled

        height: 24
        width: parent ? parent.width : 0

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorText
            font.pixelSize: 11
            text: toggle.label
        }

        Rectangle {
            id: track

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            border.color: toggle.checked ? root.colorOk : root.colorLine
            border.width: 1
            color: toggle.checked ? "#2c4632" : "#33373e"
            height: 18
            radius: 9
            width: 34

            Rectangle {
                color: toggle.checked ? root.colorOk : "#8b929c"
                height: 12
                radius: 6
                width: 12
                x: toggle.checked ? track.width - width - 3 : 3
                y: 3

                Behavior on x {
                    NumberAnimation {
                        duration: 130
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent

            onClicked: toggle.toggled()
        }
    }

    // A caption with the current value on the right, over the control itself.
    // Reading the number off the handle position is the thing this replaces.
    component Caption: Item {
        id: caption

        property string label: ""
        property string value: ""

        height: 13
        width: parent ? parent.width : 0

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorDim
            font.pixelSize: 10
            text: caption.label
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorAccent
            font.pixelSize: 10
            text: caption.value
        }
    }

    /// A slider over a persisted setting.
    component SettingSlider: Column {
        id: settingSlider

        property int decimals: 2
        property real from: 0
        property string label: ""
        property string settingName: ""
        property real stepSize: 0
        property real to: 1
        /// Overrides the numeric readout, for a setting whose number means
        /// something other than itself.
        property string valueText: ""

        readonly property alias value: slider.value

        spacing: 3
        width: parent ? parent.width : 0

        Caption {
            label: settingSlider.label
            value: settingSlider.valueText !== "" ? settingSlider.valueText : slider.value.toFixed(settingSlider.decimals)
        }

        KSlider {
            id: slider

            from: settingSlider.from
            settingName: settingSlider.settingName
            stepSize: settingSlider.stepSize
            to: settingSlider.to
            width: parent.width
        }
    }

    /// A slider over something that is not a setting: it lives on the tracker
    /// for this run only, so it is bound one way and pushed back on `moved`.
    component ValueSlider: Column {
        id: valueSlider

        property int decimals: 2
        property real from: 0
        property string label: ""
        property real to: 1
        property real value: 0

        signal moved(real value)

        spacing: 3
        width: parent ? parent.width : 0

        Caption {
            label: valueSlider.label
            value: valueSlider.value.toFixed(valueSlider.decimals)
        }

        Slider {
            id: slider

            from: valueSlider.from
            implicitHeight: 18
            to: valueSlider.to
            value: valueSlider.value
            width: parent.width

            background: Rectangle {
                color: "#3a3f47"
                height: 4
                radius: 2
                width: slider.availableWidth
                x: slider.leftPadding
                y: slider.topPadding + (slider.availableHeight / 2) - (height / 2)

                Rectangle {
                    color: root.colorAccent
                    height: parent.height
                    radius: 2
                    width: slider.visualPosition * parent.width
                }
            }
            handle: Rectangle {
                border.color: root.colorAccent
                border.width: 1
                color: slider.pressed ? "#ffffff" : "#d7dbe2"
                height: 12
                radius: 6
                width: 12
                x: slider.leftPadding + (slider.visualPosition * (slider.availableWidth - width))
                y: slider.topPadding + (slider.availableHeight / 2) - (height / 2)
            }

            onMoved: valueSlider.moved(value)
        }
    }

    /// A drop down, styled once. `textAt` rather than a role name, so the same
    /// component serves a list of strings and a list of objects.
    component Combo: ComboBox {
        id: combo

        font.pixelSize: 10
        implicitHeight: 26
        width: parent ? parent.width : 0

        background: Rectangle {
            border.color: combo.hovered ? root.colorAccent : root.colorLine
            border.width: 1
            color: root.colorRaised
            radius: 4
        }
        contentItem: Text {
            color: combo.enabled ? root.colorText : root.colorDim
            elide: Text.ElideRight
            font: combo.font
            leftPadding: 8
            rightPadding: 20
            text: combo.displayText
            verticalAlignment: Text.AlignVCenter
        }
        delegate: ItemDelegate {
            id: option

            required property int index

            height: 24
            highlighted: combo.highlightedIndex === option.index
            width: combo.width

            background: Rectangle {
                color: option.highlighted ? "#3a3f47" : "transparent"
            }
            contentItem: Text {
                color: root.colorText
                elide: Text.ElideRight
                font.pixelSize: 10
                leftPadding: 6
                text: combo.textAt(option.index)
                verticalAlignment: Text.AlignVCenter
            }
        }
        indicator: Chevron {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorDim
            open: true
        }
        popup: Popup {
            implicitHeight: Math.min(contentItem.implicitHeight + 2, 220)
            padding: 1
            width: combo.width
            y: combo.height - 1

            background: Rectangle {
                border.color: root.colorLine
                border.width: 1
                color: root.colorRaised
                radius: 4
            }
            contentItem: ListView {
                clip: true
                currentIndex: combo.highlightedIndex
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null

                ScrollIndicator.vertical: ScrollIndicator {
                }
            }
        }
    }

    component SmallButton: Rectangle {
        id: button

        property string label: ""

        signal clicked

        border.color: mouse.containsMouse ? root.colorAccent : root.colorLine
        border.width: 1
        color: mouse.pressed ? "#3a3f47" : root.colorRaised
        height: 20
        radius: 3
        width: labelText.width + 16

        Text {
            id: labelText

            anchors.centerIn: parent
            color: root.colorText
            font.pixelSize: 9
            text: button.label
        }

        MouseArea {
            id: mouse

            anchors.fill: parent
            hoverEnabled: true

            onClicked: button.clicked()
        }
    }

    // A glow colour, with a ring when it is the one in use. Without the ring
    // the panel cannot say which of four swatches is current, which it could
    // not before.
    component Swatch: Rectangle {
        id: swatch

        property color swatchColor: "transparent"
        property bool selected: false

        signal picked

        border.color: swatch.selected ? "white" : root.colorLine
        border.width: swatch.selected ? 2 : 1
        color: swatch.swatchColor
        height: 26
        radius: 4
        width: 26

        MouseArea {
            anchors.fill: parent

            onClicked: swatch.picked()
        }
    }

    // A named light. Three of them under the title say what is running without
    // opening anything.
    component StatusLight: Row {
        id: light

        property string label: ""
        property color tint: root.colorDim

        spacing: 4

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            color: light.tint
            height: 7
            radius: 3.5
            width: 7
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorDim
            font.pixelSize: 9
            text: light.label
        }
    }

    color: root.colorBackground
    visible: KSettings.showControls
    z: 100

    Component {
        id: cameraPreviewComponent

        CameraFeedDisplay {
            faceTracker: root.faceTracker
            speechShown: root.spokenShown
            speechSpeaker: root.spokenBy
            speechText: root.spokenText
        }
    }

    Component {
        id: speechDisplayComponent

        SpeechDisplay {
            speechRecognizer: root.speechRecognizer
        }
    }

    Component {
        id: skillsDisplayComponent

        SkillsDisplay {
        }
    }

    // A finished utterance, held here for a few seconds so that the preview can
    // draw it over the head of whoever said it.
    //
    // It is the joined signal and not `sentenceRecognized`, because that one
    // carries the words without the name, and the name is the whole of the
    // question. It is held rather than bound because the same sentence said
    // twice is two things that were said, and a binding on the transcript would
    // show the first of them and then sit still.
    Connections {
        function onUtteranceReady(text, confidence, speaker, priority, authorised) {
            if (speaker === "" || text === "")
                return;
            root.spokenBy = speaker;
            root.spokenText = text;
            root.spokenShown = true;
            spokenHoldTimer.restart();
        }

        target: root.speechRecognizer
    }

    Timer {
        id: spokenHoldTimer

        interval: root.spokenHoldMs

        onTriggered: root.spokenShown = false
    }

    Timer {
        id: colorAnimationTimer

        property real hue: 0

        interval: 16 // approximately 60 FPS
        repeat: true
        running: false

        onTriggered: {
            hue = (hue + 0.005) % 1.0;
            KSettings.glowColor = Qt.hsva(hue, 1.0, 1.0, 1.0);
        }
    }

    // The edge, so the panel reads as a panel over a bright face rather than as
    // a darker patch of it.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: root.colorLine
        width: 1
    }

    Item {
        id: heading

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 62

        Text {
            id: wordmark

            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 10
            color: root.colorText
            font.bold: true
            font.letterSpacing: 3
            font.pixelSize: 17
            text: "KERFUR"
        }

        Text {
            anchors.baseline: wordmark.baseline
            anchors.left: wordmark.right
            anchors.leftMargin: 7
            color: root.colorDim
            font.italic: true
            font.pixelSize: 10
            text: "by kidev"
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: wordmark.bottom
            anchors.topMargin: 7
            spacing: 12

            StatusLight {
                label: "vision"
                tint: {
                    if (!root.faceTracker || !root.faceTracker.enabled)
                        return "#4a4f57";
                    if (root.faceTracker.errorString !== "")
                        return root.colorBad;
                    return root.faceTracker.faceDetected ? root.colorOk : root.colorWarn;
                }
            }

            StatusLight {
                label: "hearing"
                tint: {
                    if (!root.speechRecognizer || !root.speechRecognizer.enabled)
                        return "#4a4f57";
                    if (root.speechRecognizer.errorString !== "")
                        return root.colorBad;
                    return root.speechRecognizer.listening ? root.colorOk : root.colorWarn;
                }
            }

            StatusLight {
                label: "skills"
                tint: Skills.problem !== "" ? root.colorBad : (Skills.skillNames.length > 0 ? root.colorOk : "#4a4f57")
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: root.colorLine
            height: 1
        }
    }

    Flickable {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: sections.height + 20
        contentWidth: width

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 6

            contentItem: Rectangle {
                color: root.colorDim
                opacity: 0.5
                radius: 3
            }
        }

        Column {
            id: sections

            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 10
            spacing: 4

            Section {
                id: visionSection

                summary: root.visionSummary()
                summaryColor: root.faceTracker && root.faceTracker.errorString !== "" ? root.colorBad : root.colorDim
                title: "Vision"

                Toggle {
                    checked: KSettings.faceTrackingEnabled
                    label: "Face tracking"

                    onToggled: KSettings.faceTrackingEnabled = !KSettings.faceTrackingEnabled
                }

                Item {
                    id: previewSlot

                    height: (width * 0.75) + 18
                    width: parent.width

                    // Loaded only while the section is open. Nothing binds to
                    // the frame property when this is inactive, and the frame
                    // is a JPEG encode and a base64 of it ten times a second.
                    Loader {
                        id: cameraPreviewLoader

                        active: visionSection.expanded
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: previewSlot.width * 0.75
                        sourceComponent: cameraPreviewComponent
                    }
                }

                Caption {
                    label: "Camera"
                    value: root.faceTracker ? root.faceTracker.currentResolution : ""
                }

                Combo {
                    id: resolutionCombo

                    enabled: root.faceTracker && root.faceTracker.availableResolutions.length > 0
                    model: root.faceTracker ? root.faceTracker.availableResolutions : []
                    // What was asked for, not what arrived: a device that
                    // substitutes a smaller mode should show the request that
                    // it ignored, next to the delivered size above.
                    currentIndex: root.faceTracker ? root.faceTracker.availableResolutions.indexOf(root.faceTracker.requestedResolution) : -1

                    onActivated: {
                        if (root.faceTracker)
                            root.faceTracker.setResolution(resolutionCombo.currentText);
                    }
                }

                Caption {
                    label: "Hands"
                    value: root.handsSummary()
                }

                Caption {
                    label: "Update rate"
                    value: root.faceTracker ? (root.faceTracker.updateHz + " Hz, " + root.faceTracker.analysisMs + " ms per pass") : ""
                }

                Combo {
                    id: updateRateCombo

                    currentIndex: root.updateRateValues.indexOf(KSettings.visionUpdateHz)
                    model: root.updateRateLabels

                    onActivated: KSettings.visionUpdateHz = root.updateRateValues[updateRateCombo.currentIndex]
                }

                Caption {
                    label: "Preview detail"
                    value: root.faceTracker ? root.faceTracker.previewResolution : ""
                }

                Combo {
                    id: previewCombo

                    currentIndex: root.previewPresetSides.indexOf(KSettings.previewMaxSide)
                    model: root.previewPresetLabels

                    onActivated: KSettings.previewMaxSide = root.previewPresetSides[previewCombo.currentIndex]
                }

                Caption {
                    label: "Analysis detail"
                    value: root.faceTracker ? root.faceTracker.analysisResolution : ""
                }

                Combo {
                    id: analysisCombo

                    currentIndex: root.analysisPresetSides.indexOf(KSettings.analysisMaxSide)
                    model: root.analysisPresetLabels

                    onActivated: KSettings.analysisMaxSide = root.analysisPresetSides[analysisCombo.currentIndex]
                }

                Text {
                    color: root.colorDim
                    font.pixelSize: 9
                    text: "The update rate is how often all of this happens: one frame, one pass of every model, one picture. The preview is the picture on this panel; the analysis is what the models are shown. Neither is ever enlarged past what the camera delivers, so both are capped by the camera line above. A larger analysis frame is what lets a face across the room, or a hand at arm's length, survive the downscale, and its cost is the milliseconds beside the rate."
                    width: parent.width
                    wrapMode: Text.WordWrap
                }

                ValueSlider {
                    label: "Detection confidence"
                    from: 0.3
                    to: 0.9
                    value: root.faceTracker ? root.faceTracker.detectionConfidence : 0.6

                    onMoved: value => {
                        if (root.faceTracker)
                            root.faceTracker.detectionConfidence = value;
                    }
                }

                ValueSlider {
                    label: "Tracking smoothing"
                    from: 0.0
                    to: 1.0
                    value: root.faceTracker ? root.faceTracker.trackingSmoothing : 0.7

                    onMoved: value => {
                        if (root.faceTracker)
                            root.faceTracker.trackingSmoothing = value;
                    }
                }

                SettingSlider {
                    decimals: 1
                    from: 0.1
                    label: "Eye movement range"
                    settingName: "eyeMovementRange"
                    to: 3.0
                }
            }

            Section {
                id: hearingSection

                summary: root.hearingSummary()
                summaryColor: root.speechRecognizer && root.speechRecognizer.errorString !== "" ? root.colorBad : root.colorDim
                title: "Hearing"

                Toggle {
                    checked: KSettings.speechRecognitionEnabled
                    label: "Listen for 'Kerfur'"

                    onToggled: KSettings.speechRecognitionEnabled = !KSettings.speechRecognitionEnabled
                }

                Item {
                    height: 14
                    width: parent.width

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.colorDim
                        font.pixelSize: 10
                        text: "Audio device"
                    }

                    SmallButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        label: "Refresh"

                        onClicked: {
                            if (root.speechRecognizer)
                                root.speechRecognizer.refreshDevices();
                        }
                    }
                }

                Combo {
                    id: audioDeviceCombo

                    model: root.speechRecognizer ? root.speechRecognizer.availableDevices : []
                    currentIndex: root.speechRecognizer ? root.speechRecognizer.availableDevices.indexOf(root.speechRecognizer.currentDevice) : -1

                    onActivated: {
                        if (root.speechRecognizer && audioDeviceCombo.currentText !== "")
                            root.speechRecognizer.setDevice(audioDeviceCombo.currentText);
                    }
                }

                Loader {
                    active: hearingSection.expanded
                    sourceComponent: speechDisplayComponent
                    width: parent.width
                }
            }

            Section {
                id: skillsSection

                summary: Skills.skillNames.length + " loaded"
                summaryColor: Skills.problem !== "" ? root.colorBad : root.colorDim
                title: "Skills"

                Loader {
                    active: skillsSection.expanded
                    sourceComponent: skillsDisplayComponent
                    width: parent.width
                }
            }

            Section {
                summary: KSettings.ledScreenLedSize.toFixed(0) + " px cells"
                title: "Screen"

                SettingSlider {
                    decimals: 1
                    from: 1
                    label: "LED size"
                    settingName: "ledScreenLedSize"
                    to: 50
                }

                SettingSlider {
                    from: 0
                    label: "Blur multiplier"
                    settingName: "blurMultiplier"
                    to: 5
                }

                SettingSlider {
                    decimals: 0
                    from: 0
                    label: "Up / down angle"
                    settingName: "upDownAngle"
                    to: 180
                }

                SettingSlider {
                    decimals: 0
                    from: 0
                    label: "Left / right angle"
                    settingName: "leftRightAngle"
                    to: 180
                }

                SettingSlider {
                    from: 0.0
                    label: "Spread at minimum"
                    settingName: "maskSpreadAtMin"
                    to: 1.0
                }

                SettingSlider {
                    from: 0.0
                    label: "Spread at maximum"
                    settingName: "maskSpreadAtMax"
                    to: 1.0
                }

                SettingSlider {
                    from: 0.0
                    label: "Threshold minimum"
                    settingName: "maskThresholdMin"
                    to: 1.0
                }

                SettingSlider {
                    from: 0.0
                    label: "Threshold maximum"
                    settingName: "maskThresholdMax"
                    to: 1.0
                }
            }

            Section {
                summary: root.blendModeNames[KSettings.glowBlendMode]
                title: "Glow"

                SettingSlider {
                    decimals: 0
                    from: 0
                    label: "Blend mode"
                    settingName: "glowBlendMode"
                    stepSize: 1
                    to: 3
                    valueText: root.blendModeNames[KSettings.glowBlendMode]
                }

                SettingSlider {
                    from: 0
                    label: "Bloom"
                    settingName: "glowBloom"
                    to: 2
                }

                SettingSlider {
                    decimals: 3
                    from: 0
                    label: "Blur"
                    settingName: "glowBlurAmount"
                    to: 1
                }

                SettingSlider {
                    from: 0
                    label: "Maximum brightness"
                    settingName: "glowMaxBrightness"
                    to: 2
                }

                Caption {
                    label: "Colour"
                    value: colorAnimationTimer.running ? "cycling" : ""
                }

                Row {
                    spacing: 8
                    width: parent.width

                    Swatch {
                        selected: !colorAnimationTimer.running && Qt.colorEqual(KSettings.glowColor, "cyan")
                        swatchColor: "cyan"

                        onPicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(0, 1, 1, 1);
                        }
                    }

                    Swatch {
                        selected: !colorAnimationTimer.running && Qt.colorEqual(KSettings.glowColor, "lime")
                        swatchColor: "lime"

                        onPicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(0, 1, 0, 1);
                        }
                    }

                    Swatch {
                        selected: !colorAnimationTimer.running && Qt.colorEqual(KSettings.glowColor, "magenta")
                        swatchColor: "magenta"

                        onPicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(1, 0, 1, 1);
                        }
                    }

                    Swatch {
                        id: gamerSwatch

                        selected: colorAnimationTimer.running

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

                        onPicked: colorAnimationTimer.running = !colorAnimationTimer.running
                    }
                }
            }
        }
    }
}
