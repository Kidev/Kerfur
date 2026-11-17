// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Core
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
/// Every section is closed until asked for. There are six of them and forty
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

    /// How long a press on the face lasts before it shows or hides this panel,
    /// quoted by the hide button's tooltip.
    property int holdToToggleMs: 3000

    // One palette, named once. The alpha lives in the background colour rather
    // than in `opacity`, because opacity on the panel fades the text with it
    // and small dim text on a bright face is the thing this panel is worst at.
    readonly property color colorAccent: "#8ad8ff"
    readonly property color colorBackground: "#f01a1c20"
    readonly property color colorBad: "#ff6b6b"
    readonly property color colorDim: "#98a0ac"
    // Text laid over a filled accent or warning colour.
    readonly property color colorInk: "#14171b"
    readonly property color colorLine: "#3c414a"
    readonly property color colorOk: "#7cff5a"
    readonly property color colorRaised: "#2c3037"
    readonly property color colorSurface: "#24272c"
    readonly property color colorText: "#eceff4"
    readonly property color colorVoice: "#c8b0ff"
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
    readonly property list<color> glowPresetColors: [Qt.rgba(1, 0, 0, 1), Qt.rgba(0, 1, 0, 1), Qt.rgba(0, 0, 1, 1), Qt.rgba(0, 1, 1, 1), Qt.rgba(1, 0, 1, 1), Qt.rgba(1, 1, 1, 1)]

    // Rank for somebody added from the panel. Zero is the only rank that may
    // give commands, so it says so; the default is two, which may not.
    readonly property var rankLabels: ["Rank 0 - may give commands", "Rank 1", "Rank 2 - default", "Rank 3", "Rank 4", "Rank 5"]
    readonly property var rankValues: [0, 1, 2, 3, 4, 5]
    readonly property int defaultRankIndex: 2

    // The last utterance, while it is still worth showing over the head of the
    // person who said it. Long enough to read a sentence twice, short enough
    // that it is gone before it can be mistaken for the current one.
    readonly property int spokenHoldMs: 6000
    property bool spokenShown: false
    property string spokenBy: ""
    property string spokenText: ""

    // The People section's state, by person id: whose card shows its photos
    // and recordings, and whose card holds the edit form. At most one form is
    // open, adding or editing, because the staged samples it saves belong to
    // the library and not to the form.
    property string openPersonId: ""
    property string editingPersonId: ""
    property bool addingPerson: false

    /// Whether a preview or analysis size can be had from the camera: neither
    /// is ever enlarged past the frames it delivers. Every size is offered
    /// until the first frame says how large those are.
    function detailAvailable(side) {
        const limit = root.faceTracker ? root.faceTracker.captureMaxSide : 0;
        return limit <= 0 || side <= limit;
    }

    /// The closed drop down's text: the chosen size, or the camera's own when
    /// the choice is larger than what the camera delivers.
    function cappedDetailText(side, label) {
        return root.detailAvailable(side) ? label : root.faceTracker.captureMaxSide + " - camera size";
    }

    function hearingSummary() {
        if (!root.speechRecognizer || !root.speechRecognizer.enabled)
            return "off";
        if (root.speechRecognizer.errorString !== "")
            return "error";
        if (root.speechRecognizer.processing)
            return "transcribing";
        if (root.speechRecognizer.listening && root.speechRecognizer.modelProblem !== "")
            return "listening, no transcription";
        if (root.speechRecognizer.listening)
            return "listening";
        return "starting";
    }

    // Red for what stops a device, amber for what the device runs without.
    function hearingColor() {
        if (!root.speechRecognizer)
            return root.colorDim;
        if (root.speechRecognizer.errorString !== "")
            return root.colorBad;
        return root.speechRecognizer.modelProblem !== "" ? root.colorWarn : root.colorDim;
    }

    function visionColor() {
        if (!root.faceTracker)
            return root.colorDim;
        if (root.faceTracker.errorString !== "")
            return root.colorBad;
        return root.faceTracker.perceptionProblem !== "" ? root.colorWarn : root.colorDim;
    }

    function isGlowPreset(colour) {
        return root.glowPresetColors.some(preset => Qt.colorEqual(preset, colour));
    }

    function peopleSummary() {
        if (!Identities.ready)
            return "loading";
        const known = root.faceTracker ? root.faceTracker.roster.length : 0;
        return known + " known, " + Identities.people.length + " added here";
    }

    function countOf(count, one, many) {
        return count + " " + (count === 1 ? one : many);
    }

    function openNewPerson() {
        Identities.clearStaged();
        root.editingPersonId = "";
        root.addingPerson = true;
    }

    // Pressing Edit on the card whose form is open closes it.
    function toggleEditPerson(id) {
        const closing = root.editingPersonId === id;
        Identities.clearStaged();
        root.addingPerson = false;
        root.editingPersonId = closing ? "" : id;
    }

    function closePersonForm() {
        Identities.clearStaged();
        root.addingPerson = false;
        root.editingPersonId = "";
    }

    // Somebody just added keeps the form, now as theirs, so another photo or
    // sentence for them is one capture and one save away.
    function personSaved(id) {
        root.addingPerson = false;
        root.editingPersonId = id;
    }

    function removePerson(id) {
        voicePlayer.stop();
        if (root.editingPersonId === id)
            root.closePersonForm();
        Identities.remove(id);
    }

    function isPlaying(url) {
        return voicePlayer.playing.toString() === url;
    }

    // The browser has no file system for a QML dialog to show, so on the web
    // build the browser's own picker is used and hands the bytes over.
    function browseIdentityFile(kind) {
        if (Identities.browserFiles) {
            if (kind === "face")
                Identities.browseFace();
            else
                Identities.browseVoice();
            return;
        }
        identityFileDialog.kind = kind;
        identityFileDialog.open();
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

        /// Whether the option at an index can be chosen. A disabled option is
        /// listed, dimmed, and ignored when picked.
        property var optionEnabled: index => true

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

            // Pinned, because the default comes from the style: Basic (the
            // browser's) pads by 12 on every side, which leaves a 24 pixel row
            // no height for its text, and an elided Text drops every line that
            // does not fit.
            enabled: combo.optionEnabled(option.index)
            height: 24
            horizontalPadding: 6
            verticalPadding: 0
            highlighted: option.enabled && combo.highlightedIndex === option.index
            width: combo.width

            background: Rectangle {
                color: option.highlighted ? "#3a3f47" : "transparent"
            }
            contentItem: Text {
                color: option.enabled ? root.colorText : "#5a606b"
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

    // A button for something that deletes: a red cross. The first press
    // fills it and, unless it is `compact`, asks; only a second press within a
    // few seconds acts. `label` is shown beside the cross while it is idle.
    component DeleteButton: Rectangle {
        id: deleteButton

        property string action: "Delete"
        property bool armed: false
        property bool compact: false
        property string label: ""

        readonly property bool showsText: !deleteButton.compact && (deleteButton.armed || deleteButton.label !== "")

        signal confirmed

        border.color: deleteButton.armed || deleteMouse.containsMouse ? root.colorBad : "#7a3a3e"
        border.width: 1
        color: {
            if (deleteButton.armed)
                return root.colorBad;
            return deleteMouse.pressed ? "#5a2a2e" : "#3a2326";
        }
        height: 20
        opacity: deleteButton.enabled ? 1 : 0.4
        radius: height / 2
        width: deleteButton.showsText ? deleteRow.implicitWidth + 14 : height

        Behavior on width {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }

        Row {
            id: deleteRow

            anchors.centerIn: parent
            spacing: 5

            Cross {
                anchors.verticalCenter: parent.verticalCenter
                color: deleteButton.armed ? root.colorInk : root.colorBad
                height: Math.round(deleteButton.height * 0.4)
                thickness: 1.8
                width: height
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: deleteButton.armed ? root.colorInk : root.colorBad
                font.bold: deleteButton.armed
                font.pixelSize: 9
                text: deleteButton.armed ? deleteButton.action + "?" : deleteButton.label
                visible: deleteButton.showsText
            }
        }

        MouseArea {
            id: deleteMouse

            anchors.fill: parent
            hoverEnabled: true

            onClicked: {
                if (deleteButton.armed) {
                    deleteButton.armed = false;
                    deleteButton.confirmed();
                    return;
                }
                deleteButton.armed = true;
                disarmTimer.restart();
            }
        }

        Timer {
            id: disarmTimer

            interval: 3000

            onTriggered: deleteButton.armed = false
        }
    }

    // A cross, drawn. Turned 45 degrees it is a delete or close mark; upright
    // it is a plus.
    component Cross: Item {
        id: cross

        property color color: "white"
        property real thickness: 1.6

        implicitHeight: 8
        implicitWidth: 8
        rotation: 45

        Rectangle {
            anchors.centerIn: parent
            antialiasing: true
            color: cross.color
            height: cross.thickness
            radius: height / 2
            width: parent.width
        }

        Rectangle {
            anchors.centerIn: parent
            antialiasing: true
            color: cross.color
            height: parent.height
            radius: width / 2
            width: cross.thickness
        }
    }

    // A play mark: the right half of a square stood on its corner, stretched
    // to the usual proportion.
    component PlayMark: Item {
        id: playMark

        property color color: "white"

        implicitHeight: 9
        implicitWidth: Math.round(implicitHeight * 0.85)

        Item {
            clip: true
            height: playMark.height
            width: playMark.height / 2

            transform: Scale {
                xScale: playMark.width / (playMark.height / 2)
            }

            Rectangle {
                antialiasing: true
                color: playMark.color
                height: playMark.height / Math.SQRT2
                rotation: 45
                width: height
                x: -width / 2
                y: (playMark.height - height) / 2
            }
        }
    }

    // A round button carrying one drawn mark: "close", "plus", "play" or
    // "stop". `tint` colours the mark and the ring.
    component MarkButton: Rectangle {
        id: markButton

        property string mark: "close"
        property color tint: root.colorText

        signal clicked

        border.color: markMouse.containsMouse ? markButton.tint : root.colorLine
        border.width: 1
        color: markMouse.pressed ? "#3a3f47" : root.colorRaised
        height: 20
        opacity: markButton.enabled ? 1 : 0.4
        radius: height / 2
        width: height

        Cross {
            anchors.centerIn: parent
            color: markButton.tint
            height: Math.round(markButton.height * 0.4)
            rotation: markButton.mark === "plus" ? 0 : 45
            visible: markButton.mark === "close" || markButton.mark === "plus"
            width: height
        }

        PlayMark {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 1
            color: markButton.tint
            implicitHeight: Math.round(markButton.height * 0.42)
            visible: markButton.mark === "play"
        }

        Rectangle {
            anchors.centerIn: parent
            color: markButton.tint
            height: Math.round(markButton.height * 0.34)
            radius: 1
            visible: markButton.mark === "stop"
            width: height
        }

        MouseArea {
            id: markMouse

            anchors.fill: parent
            hoverEnabled: true

            onClicked: markButton.clicked()
        }
    }

    // A small rounded label in a colour of its own, for the facts about a
    // person that are read at a glance: rank, photos, recordings.
    component Chip: Rectangle {
        id: chip

        property string text: ""
        property color tint: root.colorDim

        border.color: Qt.rgba(chip.tint.r, chip.tint.g, chip.tint.b, 0.55)
        border.width: 1
        color: Qt.rgba(chip.tint.r, chip.tint.g, chip.tint.b, 0.12)
        implicitHeight: 15
        implicitWidth: chipText.implicitWidth + 12
        radius: height / 2

        Text {
            id: chipText

            anchors.centerIn: parent
            color: chip.tint
            elide: Text.ElideRight
            font.pixelSize: 8
            horizontalAlignment: Text.AlignHCenter
            text: chip.text
            width: Math.min(implicitWidth, chip.width - 12)
        }
    }

    /// A text button. A `primary` one is filled with the accent colour, for
    /// the one action a form exists for.
    component SmallButton: Rectangle {
        id: button

        property string label: ""
        property bool primary: false

        signal clicked

        border.color: button.primary || mouse.containsMouse ? root.colorAccent : root.colorLine
        border.width: 1
        color: {
            if (button.primary)
                return mouse.pressed ? "#5fb4dd" : root.colorAccent;
            return mouse.pressed ? "#3a3f47" : root.colorRaised;
        }
        height: 20
        opacity: button.enabled ? 1 : 0.4
        radius: 3
        width: labelText.width + 16

        Text {
            id: labelText

            anchors.centerIn: parent
            color: button.primary ? root.colorInk : root.colorText
            font.bold: button.primary
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

    component Field: TextField {
        id: field

        color: root.colorText
        font.pixelSize: 10
        implicitHeight: 26
        leftPadding: 8
        placeholderTextColor: root.colorDim
        width: parent ? parent.width : 0

        background: Rectangle {
            border.color: field.activeFocus ? root.colorAccent : root.colorLine
            border.width: 1
            color: root.colorRaised
            radius: 4
        }
    }

    // An enrolled photo, cropped square. The initial stands in while it
    // loads and when it cannot be read.
    component FaceThumbnail: Rectangle {
        id: thumbnail

        property string fallback: ""
        property int size: 30
        property alias source: picture.source

        border.color: root.colorLine
        border.width: 1
        clip: true
        color: root.colorSurface
        height: thumbnail.size
        radius: 3
        width: thumbnail.size

        Text {
            anchors.centerIn: parent
            color: root.colorDim
            font.bold: true
            font.pixelSize: Math.round(thumbnail.size * 0.45)
            text: thumbnail.fallback.length > 0 ? thumbnail.fallback.charAt(0).toUpperCase() : "?"
            visible: picture.status !== Image.Ready
        }

        Image {
            id: picture

            anchors.fill: parent
            anchors.margins: 1
            asynchronous: true
            // A sample deleted and taken again reuses its file name.
            cache: false
            fillMode: Image.PreserveAspectCrop
            sourceSize: Qt.size(thumbnail.size * 2, thumbnail.size * 2)
        }
    }

    // A caption over a form field.
    component FieldLabel: Text {
        color: root.colorDim
        font.pixelSize: 9
        width: parent ? parent.width : 0
    }

    // What is staged for one kind of sample, and the two ways to stage it.
    component StagingRow: Item {
        id: stagingRow

        property string label: ""
        property string staged: ""

        height: 15
        width: parent ? parent.width : 0

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: root.colorText
            font.pixelSize: 10
            text: stagingRow.label
        }

        Chip {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: stagingRow.staged !== "" ? stagingRow.staged : "nothing staged"
            tint: stagingRow.staged !== "" ? root.colorOk : root.colorDim
            width: Math.min(implicitWidth, stagingRow.width * 0.7)
        }
    }

    // The form a person is added or edited with. `person` is an entry of
    // `Identities.people` when editing, and unset when adding somebody new.
    component PersonForm: Rectangle {
        id: form

        property var person: undefined

        readonly property bool editing: form.person !== undefined && form.person !== null
        /// The enrolled person the typed name resolves to, when adding. The
        /// same folding `enrol` applies, so "Ada" and "ada" are one person here
        /// as they are on disk.
        readonly property var existing: {
            if (form.editing)
                return form.person;
            const id = Identities.personIdFor(nameField.text);
            if (id === "")
                return undefined;
            return Identities.people.find(entry => entry.id === id);
        }

        signal closed
        signal saved(string id)

        function save() {
            const name = form.editing ? form.person.name : nameField.text;
            if (Identities.enrol(name, titleField.text, root.rankValues[rankCombo.currentIndex]))
                form.saved(Identities.personIdFor(name));
        }

        border.color: root.colorAccent
        border.width: 1
        color: "#1d2126"
        height: formColumn.implicitHeight + 16
        radius: 4
        width: parent ? parent.width : 0

        Component.onCompleted: {
            if (!form.editing)
                return;
            titleField.text = form.person.title;
            const rank = root.rankValues.indexOf(form.person.priority);
            rankCombo.currentIndex = rank >= 0 ? rank : root.defaultRankIndex;
        }

        Column {
            id: formColumn

            anchors.left: parent.left
            anchors.margins: 8
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 5

            Item {
                height: 20
                width: parent.width

                Text {
                    anchors.left: parent.left
                    anchors.right: closeForm.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.colorAccent
                    elide: Text.ElideRight
                    font.bold: true
                    font.pixelSize: 11
                    text: form.editing ? "Edit " + form.person.name : "New person"
                }

                MarkButton {
                    id: closeForm

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 18
                    mark: "close"
                    tint: root.colorDim

                    onClicked: form.closed()
                }
            }

            Text {
                color: root.colorDim
                font.pixelSize: 9
                text: form.editing ? "Change the title or the rank, or stage another photo or recording. Each save adds them as one more sample, up to " + Identities.sampleLimit + " of each." : "A name and at least a photo or a recording. A few samples taken in different light and rooms make recognition more reliable."
                width: parent.width
                wrapMode: Text.WordWrap
            }

            FieldLabel {
                text: "Name"
                visible: !form.editing
            }

            Field {
                id: nameField

                maximumLength: 64
                placeholderText: "Name"
                visible: !form.editing
            }

            Text {
                color: root.colorWarn
                font.pixelSize: 9
                text: form.existing ? form.existing.name + " is already known: saving adds these samples to them." : ""
                visible: !form.editing && form.existing !== undefined
                width: parent.width
                wrapMode: Text.WordWrap
            }

            FieldLabel {
                text: "Title shown on screen"
            }

            Field {
                id: titleField

                maximumLength: 64
                placeholderText: "owner, friend, ..."
            }

            FieldLabel {
                text: "Rank"
            }

            Combo {
                id: rankCombo

                currentIndex: root.defaultRankIndex
                model: root.rankLabels
            }

            Item {
                height: 2
                width: parent.width
            }

            StagingRow {
                label: "Photo"
                staged: Identities.stagedFace
            }

            Flow {
                spacing: 6
                width: parent.width

                SmallButton {
                    enabled: root.faceTracker !== null && root.faceTracker.enabled
                    label: "Take from camera"

                    onClicked: Identities.stageFaceImage(root.faceTracker.cameraFrame, "camera")
                }

                SmallButton {
                    label: "Choose a picture"

                    onClicked: root.browseIdentityFile("face")
                }
            }

            StagingRow {
                label: "Voice"
                staged: Identities.stagedVoice
            }

            Flow {
                spacing: 6
                width: parent.width

                SmallButton {
                    enabled: root.speechRecognizer !== null && root.speechRecognizer.lastUtteranceSeconds >= root.speechRecognizer.minimumVoiceSeconds
                    label: root.speechRecognizer ? "Use last sentence (" + root.speechRecognizer.lastUtteranceSeconds.toFixed(1) + " s)" : "Use last sentence"

                    onClicked: root.speechRecognizer.stageLastUtterance()
                }

                SmallButton {
                    label: "Choose a recording"

                    onClicked: root.browseIdentityFile("voice")
                }
            }

            Text {
                color: root.colorBad
                font.pixelSize: 9
                text: Identities.problem
                visible: Identities.problem !== ""
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Item {
                height: 2
                width: parent.width
            }

            Flow {
                spacing: 6
                width: parent.width

                SmallButton {
                    enabled: Identities.ready && (form.editing || nameField.text.trim() !== "")
                    label: form.editing || form.existing ? "Save" : "Save person"
                    primary: true

                    onClicked: form.save()
                }

                SmallButton {
                    enabled: Identities.stagedFace !== "" || Identities.stagedVoice !== ""
                    label: "Clear staged"

                    onClicked: Identities.clearStaged()
                }

                SmallButton {
                    label: form.editing ? "Done" : "Cancel"

                    onClicked: form.closed()
                }
            }
        }
    }

    // A glow colour, with a ring when it is the one in use. Without the ring
    // the panel cannot say which swatch is current.
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

    FileDialog {
        id: identityFileDialog

        property string kind: "face"

        nameFilters: identityFileDialog.kind === "face" ? ["Pictures (*.png *.jpg *.jpeg *.bmp)"] : ["Recordings (*.wav *.mp3 *.flac *.ogg *.m4a)"]
        title: identityFileDialog.kind === "face" ? "Choose a picture" : "Choose a recording"

        onAccepted: {
            if (identityFileDialog.kind === "face")
                Identities.stageFaceFile(identityFileDialog.selectedFile);
            else
                Identities.stageVoiceFile(identityFileDialog.selectedFile);
        }
    }

    ColorDialog {
        id: glowColorDialog

        title: "Choose a glow colour"

        onAccepted: {
            colorAnimationTimer.running = false;
            const picked = glowColorDialog.selectedColor;
            KSettings.glowCustomColor = Qt.rgba(picked.r, picked.g, picked.b, 1);
            KSettings.glowColor = KSettings.glowCustomColor;
        }
    }

    // Plays back an enrolled recording from the People section.
    VoicePlayer {
        id: voicePlayer
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
        height: Session.light ? 40 : 62

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
            visible: !Session.light

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
            id: hideButton

            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: wordmark.verticalCenter
            border.color: hideMouse.containsMouse ? root.colorAccent : root.colorLine
            border.width: 1
            color: hideMouse.pressed ? "#3a3f47" : root.colorRaised
            height: 22
            radius: 4
            width: 22

            Chevron {
                anchors.centerIn: parent
                color: hideMouse.containsMouse ? root.colorAccent : root.colorDim
                height: 11
                width: 11
            }

            MouseArea {
                id: hideMouse

                anchors.fill: parent
                hoverEnabled: true

                onClicked: KSettings.showControls = false
            }

            ToolTip {
                id: hideTip

                delay: 300
                text: "Hide this panel. Press Tab, or click or touch the face and hold it for " + Math.round(root.holdToToggleMs / 1000) + "s, to show or hide it."
                visible: hideMouse.containsMouse
                width: Math.min(implicitWidth, 220)
                x: hideButton.width - width
                y: hideButton.height + 6

                background: Rectangle {
                    border.color: root.colorLine
                    border.width: 1
                    color: root.colorRaised
                    radius: 4
                }
                contentItem: Text {
                    color: root.colorText
                    font.pixelSize: 10
                    text: hideTip.text
                    wrapMode: Text.WordWrap
                }
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
                summaryColor: root.visionColor()
                title: "Vision"
                visible: !Session.light

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

                Text {
                    color: root.colorWarn
                    font.pixelSize: 9
                    text: root.faceTracker ? root.faceTracker.perceptionProblem : ""
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                }

                Caption {
                    label: "Camera"
                    value: root.faceTracker ? root.faceTracker.currentResolution : ""
                }

                Combo {
                    id: cameraCombo

                    currentIndex: root.faceTracker ? root.faceTracker.availableCameras.indexOf(root.faceTracker.currentCamera) : -1
                    model: root.faceTracker ? root.faceTracker.availableCameras : []
                    visible: root.faceTracker ? root.faceTracker.availableCameras.length > 0 : false

                    onActivated: {
                        if (root.faceTracker)
                            KSettings.cameraId = root.faceTracker.cameraIdFor(cameraCombo.currentText);
                    }
                }

                Combo {
                    id: resolutionCombo

                    model: root.faceTracker ? root.faceTracker.availableResolutions : []
                    visible: root.faceTracker ? root.faceTracker.availableResolutions.length > 0 : false
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
                    displayText: root.cappedDetailText(KSettings.previewMaxSide, previewCombo.currentText)
                    model: root.previewPresetLabels
                    optionEnabled: index => root.detailAvailable(root.previewPresetSides[index])

                    onActivated: index => {
                        if (previewCombo.optionEnabled(index))
                            KSettings.previewMaxSide = root.previewPresetSides[index];
                        previewCombo.currentIndex = Qt.binding(() => root.previewPresetSides.indexOf(KSettings.previewMaxSide));
                    }
                }

                Caption {
                    label: "Analysis detail"
                    value: root.faceTracker ? root.faceTracker.analysisResolution : ""
                }

                Combo {
                    id: analysisCombo

                    currentIndex: root.analysisPresetSides.indexOf(KSettings.analysisMaxSide)
                    displayText: root.cappedDetailText(KSettings.analysisMaxSide, analysisCombo.currentText)
                    model: root.analysisPresetLabels
                    optionEnabled: index => root.detailAvailable(root.analysisPresetSides[index])

                    onActivated: index => {
                        if (analysisCombo.optionEnabled(index))
                            KSettings.analysisMaxSide = root.analysisPresetSides[index];
                        analysisCombo.currentIndex = Qt.binding(() => root.analysisPresetSides.indexOf(KSettings.analysisMaxSide));
                    }
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
                    value: root.faceTracker ? root.faceTracker.trackingSmoothing : 0.0

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

                // People the gallery knows always have their hands tracked.
                Toggle {
                    checked: KSettings.loneStrangerHands
                    label: "Hands of anyone alone in view"

                    onToggled: KSettings.loneStrangerHands = !KSettings.loneStrangerHands
                }
            }

            Section {
                id: hearingSection

                summary: root.hearingSummary()
                summaryColor: root.hearingColor()
                title: "Hearing"
                visible: !Session.light

                Toggle {
                    checked: KSettings.speechRecognitionEnabled
                    label: "Listen to voices"

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

                Text {
                    color: root.colorWarn
                    font.pixelSize: 9
                    text: root.speechRecognizer ? root.speechRecognizer.modelProblem : ""
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                }

                Loader {
                    active: hearingSection.expanded
                    sourceComponent: speechDisplayComponent
                    width: parent.width
                }
            }

            Section {
                id: peopleSection

                summary: root.peopleSummary()
                summaryColor: Identities.problem !== "" ? root.colorWarn : root.colorDim
                title: "People"
                visible: !Session.light

                onExpandedChanged: {
                    if (!peopleSection.expanded)
                        voicePlayer.stop();
                }

                // One card per person: the newest photo, the name, what they
                // are to Kerfur, and the two actions taken on the whole person.
                // Pressing the card opens it on every photo and recording.
                Repeater {
                    model: Identities.people

                    delegate: Rectangle {
                        id: personCard

                        required property var modelData

                        readonly property bool open: root.openPersonId === personCard.modelData.id
                        readonly property bool editing: root.editingPersonId === personCard.modelData.id
                        readonly property var faces: personCard.modelData.faces
                        readonly property var voices: personCard.modelData.voices

                        border.color: personCard.open || personCard.editing ? root.colorAccent : (rowMouse.containsMouse ? "#4c525c" : root.colorLine)
                        border.width: 1
                        color: root.colorRaised
                        height: cardColumn.implicitHeight + 12
                        radius: 4
                        width: parent ? parent.width : 0

                        Column {
                            id: cardColumn

                            anchors.left: parent.left
                            anchors.margins: 6
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 6

                            Item {
                                height: Math.max(42, personText.implicitHeight)
                                width: parent.width

                                MouseArea {
                                    id: rowMouse

                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true

                                    onClicked: root.openPersonId = personCard.open ? "" : personCard.modelData.id
                                }

                                FaceThumbnail {
                                    id: avatar

                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    fallback: personCard.modelData.name
                                    size: 42
                                    source: personCard.faces.length > 0 ? personCard.faces[personCard.faces.length - 1] : ""
                                }

                                Column {
                                    id: personText

                                    anchors.left: avatar.right
                                    anchors.leftMargin: 8
                                    anchors.right: personActions.left
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3

                                    Text {
                                        color: root.colorText
                                        elide: Text.ElideRight
                                        font.bold: true
                                        font.pixelSize: 11
                                        text: personCard.modelData.name
                                        width: parent.width
                                    }

                                    Text {
                                        color: root.colorDim
                                        elide: Text.ElideRight
                                        font.italic: true
                                        font.pixelSize: 9
                                        text: personCard.modelData.title
                                        visible: text !== ""
                                        width: parent.width
                                    }

                                    Flow {
                                        spacing: 3
                                        width: parent.width

                                        Chip {
                                            text: personCard.modelData.priority === 0 ? "rank 0 - commands" : "rank " + personCard.modelData.priority
                                            tint: personCard.modelData.priority === 0 ? root.colorWarn : root.colorDim
                                        }

                                        Chip {
                                            text: root.countOf(personCard.modelData.faceCount, "photo", "photos")
                                            tint: personCard.modelData.faceCount > 0 ? root.colorAccent : root.colorDim
                                        }

                                        Chip {
                                            text: root.countOf(personCard.modelData.voiceCount, "voice", "voices")
                                            tint: personCard.modelData.voiceCount > 0 ? root.colorVoice : root.colorDim
                                        }
                                    }
                                }

                                Row {
                                    id: personActions

                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5

                                    Chevron {
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: personCard.open ? root.colorAccent : root.colorDim
                                        open: personCard.open
                                    }

                                    SmallButton {
                                        anchors.verticalCenter: parent.verticalCenter
                                        enabled: Identities.ready
                                        label: personCard.editing ? "Close" : "Edit"

                                        onClicked: root.toggleEditPerson(personCard.modelData.id)
                                    }

                                    DeleteButton {
                                        action: "Delete"
                                        anchors.verticalCenter: parent.verticalCenter

                                        onConfirmed: root.removePerson(personCard.modelData.id)
                                    }
                                }
                            }

                            // Every photo and recording, each with its own
                            // delete, under a heading that can delete them all.
                            Column {
                                spacing: 6
                                visible: personCard.open
                                width: parent.width

                                Rectangle {
                                    color: root.colorLine
                                    height: 1
                                    width: parent.width
                                }

                                Item {
                                    height: 20
                                    width: parent.width

                                    Text {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: root.colorAccent
                                        font.bold: true
                                        font.pixelSize: 10
                                        text: "Photos  " + personCard.faces.length + "/" + Identities.sampleLimit
                                    }

                                    DeleteButton {
                                        action: "Delete all"
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 18
                                        label: "Delete all"
                                        visible: personCard.faces.length > 1

                                        onConfirmed: Identities.forgetFaces(personCard.modelData.id)
                                    }
                                }

                                Text {
                                    color: root.colorDim
                                    font.pixelSize: 9
                                    text: "No photo yet. Edit to add one."
                                    visible: personCard.faces.length === 0
                                }

                                Flow {
                                    spacing: 6
                                    visible: personCard.faces.length > 0
                                    width: parent.width

                                    Repeater {
                                        model: personCard.open ? personCard.faces : []

                                        delegate: Item {
                                            id: photo

                                            required property string modelData

                                            height: 48
                                            width: 48

                                            FaceThumbnail {
                                                fallback: personCard.modelData.name
                                                size: 48
                                                source: photo.modelData
                                            }

                                            DeleteButton {
                                                action: "Delete"
                                                anchors.right: parent.right
                                                anchors.rightMargin: 2
                                                anchors.top: parent.top
                                                anchors.topMargin: 2
                                                compact: true
                                                height: 16

                                                onConfirmed: Identities.forgetSample(personCard.modelData.id, photo.modelData)
                                            }
                                        }
                                    }
                                }

                                Item {
                                    height: 20
                                    width: parent.width

                                    Text {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: root.colorVoice
                                        font.bold: true
                                        font.pixelSize: 10
                                        text: "Voices  " + personCard.voices.length + "/" + Identities.sampleLimit
                                    }

                                    DeleteButton {
                                        action: "Delete all"
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 18
                                        label: "Delete all"
                                        visible: personCard.voices.length > 1

                                        onConfirmed: {
                                            voicePlayer.stop();
                                            Identities.forgetVoices(personCard.modelData.id);
                                        }
                                    }
                                }

                                Text {
                                    color: root.colorDim
                                    font.pixelSize: 9
                                    text: "No recording yet. Edit to add one."
                                    visible: personCard.voices.length === 0
                                }

                                Repeater {
                                    model: personCard.open ? personCard.voices : []

                                    delegate: Rectangle {
                                        id: recording

                                        required property int index
                                        required property string modelData

                                        readonly property bool playing: root.isPlaying(recording.modelData)

                                        border.color: recording.playing ? root.colorOk : root.colorLine
                                        border.width: 1
                                        color: root.colorSurface
                                        height: 26
                                        radius: 13
                                        width: parent ? parent.width : 0

                                        MarkButton {
                                            id: playButton

                                            anchors.left: parent.left
                                            anchors.leftMargin: 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            mark: recording.playing ? "stop" : "play"
                                            tint: root.colorOk

                                            onClicked: voicePlayer.toggle(recording.modelData)
                                        }

                                        Text {
                                            anchors.left: playButton.right
                                            anchors.leftMargin: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: recording.playing ? root.colorOk : root.colorText
                                            font.pixelSize: 10
                                            text: "Recording " + (recording.index + 1) + (recording.playing ? "  playing" : "")
                                        }

                                        DeleteButton {
                                            action: "Delete"
                                            anchors.right: parent.right
                                            anchors.rightMargin: 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            compact: true

                                            onConfirmed: {
                                                if (recording.playing)
                                                    voicePlayer.stop();
                                                Identities.forgetSample(personCard.modelData.id, recording.modelData);
                                            }
                                        }
                                    }
                                }

                                Text {
                                    color: root.colorBad
                                    font.pixelSize: 9
                                    text: voicePlayer.problem
                                    visible: voicePlayer.problem !== ""
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Loader {
                                active: personCard.editing
                                visible: active
                                width: parent.width

                                sourceComponent: Component {
                                    PersonForm {
                                        person: personCard.modelData

                                        onClosed: root.closePersonForm()
                                        onSaved: id => root.personSaved(id)
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    color: root.colorDim
                    font.pixelSize: 9
                    text: "Kerfur knows nobody yet."
                    visible: Identities.people.length === 0 && !root.addingPerson
                    width: parent.width
                    wrapMode: Text.WordWrap
                }

                // Opens the form for somebody new. Hidden while that form is
                // open, which has its own close.
                Rectangle {
                    border.color: addPersonMouse.containsMouse ? root.colorAccent : "#3f6578"
                    border.width: 1
                    color: addPersonMouse.pressed ? "#23343f" : "#1b262d"
                    height: 28
                    opacity: Identities.ready ? 1 : 0.5
                    radius: 4
                    visible: !root.addingPerson
                    width: parent.width

                    Row {
                        anchors.centerIn: parent
                        spacing: 7

                        Cross {
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.colorAccent
                            height: 9
                            rotation: 0
                            thickness: 1.8
                            width: 9
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.colorAccent
                            font.bold: true
                            font.pixelSize: 10
                            text: Identities.ready ? "Add a person" : "Loading people"
                        }
                    }

                    MouseArea {
                        id: addPersonMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: Identities.ready
                        hoverEnabled: true

                        onClicked: root.openNewPerson()
                    }
                }

                Loader {
                    active: root.addingPerson
                    visible: active
                    width: parent.width

                    sourceComponent: Component {
                        PersonForm {
                            onClosed: root.closePersonForm()
                            onSaved: id => root.personSaved(id)
                        }
                    }
                }

                // A refusal from an action outside the forms, such as a
                // delete. A form shows its own.
                Text {
                    color: root.colorBad
                    font.pixelSize: 9
                    text: Identities.problem
                    visible: Identities.problem !== "" && !root.addingPerson && root.editingPersonId === ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                }
            }

            Section {
                id: skillsSection

                summary: Skills.skillNames.length + " loaded"
                summaryColor: Skills.problem !== "" ? root.colorBad : root.colorDim
                title: "Skills"
                visible: !Session.light

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

                Caption {
                    label: "Blend mode"
                }

                Combo {
                    id: blendModeCombo

                    currentIndex: KSettings.glowBlendMode
                    model: root.blendModeNames

                    onActivated: KSettings.glowBlendMode = blendModeCombo.currentIndex
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
                    value: colorAnimationTimer.running ? "gaming" : ""
                }

                Flow {
                    spacing: 8
                    width: parent.width

                    Repeater {
                        model: root.glowPresetColors

                        Swatch {
                            id: presetSwatch

                            required property color modelData

                            selected: !colorAnimationTimer.running && Qt.colorEqual(KSettings.glowColor, presetSwatch.modelData)
                            swatchColor: presetSwatch.modelData

                            onPicked: {
                                colorAnimationTimer.running = false;
                                KSettings.glowColor = presetSwatch.modelData;
                            }
                        }
                    }

                    // The custom colour: shows the last one chosen, and opens
                    // the dialog on that colour to change it.
                    Swatch {
                        selected: !colorAnimationTimer.running && !root.isGlowPreset(KSettings.glowColor) && Qt.colorEqual(KSettings.glowColor, KSettings.glowCustomColor)
                        swatchColor: KSettings.glowCustomColor

                        onPicked: {
                            glowColorDialog.selectedColor = KSettings.glowCustomColor;
                            glowColorDialog.open();
                        }

                        Text {
                            anchors.centerIn: parent
                            color: KSettings.glowCustomColor.hslLightness > 0.6 ? root.colorInk : root.colorText
                            font.bold: true
                            font.pixelSize: 14
                            text: "+"
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
