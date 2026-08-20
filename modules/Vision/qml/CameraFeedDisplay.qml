// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var faceTracker

    // Unknown, known, and Kerfur's own. The colour is the fastest thing to read
    // on a preview this small: the name has to be read, the colour only seen.
    readonly property color knownColor: "#7cff5a"
    readonly property color kidevColor: "#ff44ff"
    readonly property color unknownColor: "#00e5ff"

    // The last thing that was said, who said it, and whether it is still recent
    // enough to show. Set by whoever owns both the camera and the microphone,
    // because this view owns neither: it knows where the heads are, which is
    // the one thing the other two cannot answer.
    //
    // An utterance with no name attached is not passed down at all. Without a
    // name there is no head it belongs above, and a caption over a stranger, or
    // over nobody, would be a claim rather than an observation.
    property bool speechShown: false
    property string speechSpeaker: ""
    property string speechText: ""

    function identityColor(face) {
        if (!face || !face.known)
            return root.unknownColor;
        return face.identity === "kidev" ? root.kidevColor : root.knownColor;
    }

    // Everything the analysis reports is normalized to the frame it was
    // measured in, so an overlay only ever needs to know where the image was
    // actually painted. That is the whole of the coordinate handling, and it is
    // the same for the inline view and for the popup.
    component PerceptionOverlay: Item {
        id: overlay

        required property CameraFrameView view
        required property var tracker
        property real dotSize: 5
        property real lineWidth: 2
        property bool showLabels: true
        property real labelSize: 9

        // The utterance being shown over its speaker's head, if anybody is
        // saying anything. Passed down rather than looked up: this component
        // knows where the heads are and nothing else, which is the only reason
        // it is the one drawing this.
        property string speechSpeaker: ""
        property string speechText: ""
        property bool speechShown: false

        // The one piece of coordinate handling in this file: everything the
        // analysis reports is normalized to the frame it was measured in, so an
        // overlay only needs to know where that frame was painted. The view
        // publishes that rectangle rather than the overlay deriving it, because
        // the view is the thing that decided it.
        readonly property rect painted: view.paintedRect
        readonly property bool live: view.hasFrame && painted.width > 0 && tracker
                                     && tracker.enabled

        function px(nx) {
            return painted.x + nx * painted.width;
        }
        function py(ny) {
            return painted.y + ny * painted.height;
        }

        // The bones, flattened into one segment list so that a single Repeater
        // draws every person. Rebuilt when the people change rather than bound
        // per delegate, because a binding per bone is seventeen bindings per
        // person per frame.
        readonly property var bones: {
            if (!tracker || !tracker.people)
                return [];
            const edges = tracker.skeletonEdges;
            const out = [];
            for (let p = 0; p < tracker.people.length; ++p) {
                const points = tracker.people[p].keypoints;
                for (let e = 0; e < edges.length; ++e) {
                    const a = points[edges[e][0]];
                    const b = points[edges[e][1]];
                    if (!a || !b || a.score < 0.4 || b.score < 0.4)
                        continue;
                    out.push({
                        "ax": a.x,
                        "ay": a.y,
                        "bx": b.x,
                        "by": b.y
                    });
                }
            }
            return out;
        }

        readonly property var joints: {
            if (!tracker || !tracker.people)
                return [];
            const out = [];
            for (let p = 0; p < tracker.people.length; ++p) {
                const points = tracker.people[p].keypoints;
                for (let k = 0; k < points.length; ++k) {
                    if (points[k].score >= 0.4)
                        out.push(points[k]);
                }
            }
            return out;
        }

        // Hands and what is in them, flattened the same way the bones are.
        // Only recognised people have either: the analysis does not locate a
        // stranger's hands at all, so an empty list here is a statement about
        // who is in the room rather than about the pose model.
        readonly property var hands: {
            if (!tracker || !tracker.people)
                return [];
            const out = [];
            for (let p = 0; p < tracker.people.length; ++p) {
                const person = tracker.people[p];
                if (!person.known)
                    continue;
                for (let h = 0; h < person.hands.length; ++h) {
                    const hand = person.hands[h];
                    out.push({
                        "box": hand.box,
                        "side": hand.side,
                        "identity": person.identity,
                        "confidence": hand.confidence,
                        "landmarks": hand.landmarks,
                        "grip": hand.grip,
                        "gesture": hand.gesture,
                        "tracked": hand.tracked
                    });
                }
            }
            return out;
        }

        // The hand bones and their joints, flattened exactly the way the body's
        // are and for the same reason: twenty-one segments per hand bound one
        // delegate at a time is twenty-one bindings that always change
        // together, so they are built once per frame instead.
        readonly property var handBones: {
            const edges = tracker ? tracker.handEdges : [];
            const out = [];
            for (let h = 0; h < overlay.hands.length; ++h) {
                const points = overlay.hands[h].landmarks;
                if (!points || points.length === 0)
                    continue;
                for (let e = 0; e < edges.length; ++e) {
                    const a = points[edges[e][0]];
                    const b = points[edges[e][1]];
                    if (!a || !b)
                        continue;
                    out.push({
                        "ax": a.x,
                        "ay": a.y,
                        "bx": b.x,
                        "by": b.y
                    });
                }
            }
            return out;
        }

        // The five fingertips and the wrist, drawn a little larger than the
        // knuckles. They are the points a person actually reads a hand by, and
        // at preview size a uniform constellation is just a smudge.
        readonly property var handJoints: {
            const anchors = [0, 4, 8, 12, 16, 20];
            const out = [];
            for (let h = 0; h < overlay.hands.length; ++h) {
                const points = overlay.hands[h].landmarks;
                for (let k = 0; k < points.length; ++k) {
                    out.push({
                        "x": points[k].x,
                        "y": points[k].y,
                        "anchor": anchors.indexOf(k) >= 0
                    });
                }
            }
            return out;
        }

        readonly property var held: {
            if (!tracker || !tracker.people)
                return [];
            const out = [];
            for (let p = 0; p < tracker.people.length; ++p) {
                const person = tracker.people[p];
                for (let h = 0; h < person.held.length; ++h) {
                    const object = person.held[h];
                    out.push({
                        "box": object.box,
                        "label": object.label,
                        "score": object.score,
                        "identity": person.identity
                    });
                }
            }
            return out;
        }

        anchors.fill: view
        visible: live

        // The bottom layer, under the skeletons and under the object boxes: a
        // hand box is a region of interest rather than a detection, and it
        // should not draw the eye away from the thing that was found in it.
        Repeater {
            model: overlay.hands

            delegate: Rectangle {
                id: handBox

                required property var modelData

                border.width: 1
                color: "transparent"
                height: overlay.py(modelData.box.y + modelData.box.height) - y
                opacity: 0.75
                radius: 2
                width: overlay.px(modelData.box.x + modelData.box.width) - x
                x: overlay.px(modelData.box.x)
                y: overlay.py(modelData.box.y)

                // Bright once the landmark model has actually seen this hand,
                // dimmed while it is still the forearm estimate. The two are
                // drawn differently on purpose: one is a measurement and the
                // other is a guess about where to look, and an overlay that
                // renders them identically invites trusting the guess.
                border.color: modelData.tracked ? "#40c4ff" : "#4080a0"

                // The side is the subject's own, not the picture's: this is
                // their left hand even though the mirrored preview puts it on
                // the right of the frame. The shape is shown in preference to
                // open and closed, which is the same measurement with less in
                // it.
                Text {
                    anchors.bottom: parent.top
                    anchors.left: parent.left
                    color: "#40c4ff"
                    font.pixelSize: overlay.labelSize - 1
                    visible: overlay.showLabels

                    text: {
                        if (!handBox.modelData.tracked)
                            return handBox.modelData.side;
                        if (handBox.modelData.gesture !== "")
                            return handBox.modelData.side + "  " + handBox.modelData.gesture;
                        return handBox.modelData.side + "  " + (handBox.modelData.grip >= 0.4 ? "closed" : "open");
                    }
                }
            }
        }

        Repeater {
            model: overlay.held

            delegate: Rectangle {
                id: heldBox

                required property var modelData

                border.color: "#ffd740"
                border.width: overlay.lineWidth
                color: "transparent"
                height: overlay.py(modelData.box.y + modelData.box.height) - y
                radius: 2
                width: overlay.px(modelData.box.x + modelData.box.width) - x
                x: overlay.px(modelData.box.x)
                y: overlay.py(modelData.box.y)

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.bottom
                    anchors.topMargin: 2
                    color: "#c8000000"
                    height: heldLabel.height + 4
                    radius: 2
                    visible: overlay.showLabels
                    width: heldLabel.width + 8

                    Text {
                        id: heldLabel

                        anchors.centerIn: parent
                        color: "#ffd740"
                        font.pixelSize: overlay.labelSize
                        text: heldBox.modelData.identity + " holds " + heldBox.modelData.label
                    }
                }
            }
        }

        Repeater {
            model: overlay.bones

            delegate: Rectangle {
                required property var modelData

                readonly property real x1: overlay.px(modelData.ax)
                readonly property real y1: overlay.py(modelData.ay)
                readonly property real x2: overlay.px(modelData.bx)
                readonly property real y2: overlay.py(modelData.by)

                antialiasing: true
                color: "#ffb300"
                height: overlay.lineWidth
                opacity: 0.85
                radius: overlay.lineWidth / 2
                transformOrigin: Item.Left
                width: Math.hypot(x2 - x1, y2 - y1)
                x: x1
                y: y1 - height / 2
                rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
            }
        }

        Repeater {
            model: overlay.joints

            delegate: Rectangle {
                required property var modelData

                color: "#ffe082"
                height: overlay.dotSize
                radius: height / 2
                width: overlay.dotSize
                x: overlay.px(modelData.x) - width / 2
                y: overlay.py(modelData.y) - height / 2
            }
        }

        // Over the arm it hangs off, because the hand is the fine structure and
        // the forearm bone runs straight through it. Only ever present for a
        // hand the landmark model measured: the forearm estimate has a box and
        // nothing inside it, which is the visible difference between the two.
        Repeater {
            model: overlay.handBones

            delegate: Rectangle {
                required property var modelData

                readonly property real x1: overlay.px(modelData.ax)
                readonly property real y1: overlay.py(modelData.ay)
                readonly property real x2: overlay.px(modelData.bx)
                readonly property real y2: overlay.py(modelData.by)

                antialiasing: true
                color: "#40c4ff"
                height: Math.max(1, overlay.lineWidth - 1)
                opacity: 0.9
                radius: height / 2
                rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
                transformOrigin: Item.Left
                width: Math.hypot(x2 - x1, y2 - y1)
                x: x1
                y: y1 - height / 2
            }
        }

        Repeater {
            model: overlay.handJoints

            delegate: Rectangle {
                required property var modelData

                readonly property real side: modelData.anchor ? Math.max(3, overlay.dotSize - 1) : Math.max(2, overlay.dotSize - 3)

                color: modelData.anchor ? "#ffffff" : "#b3e5fc"
                height: side
                radius: height / 2
                width: side
                x: overlay.px(modelData.x) - width / 2
                y: overlay.py(modelData.y) - height / 2
            }
        }

        Repeater {
            model: overlay.tracker ? overlay.tracker.faces : []

            delegate: Item {
                id: faceItem

                required property var modelData

                readonly property color tint: root.identityColor(modelData)

                anchors.fill: parent

                Rectangle {
                    id: faceBox

                    border.color: faceItem.tint
                    border.width: overlay.lineWidth
                    color: "transparent"
                    height: overlay.py(faceItem.modelData.box.y + faceItem.modelData.box.height) - y
                    radius: 2
                    width: overlay.px(faceItem.modelData.box.x + faceItem.modelData.box.width) - x
                    x: overlay.px(faceItem.modelData.box.x)
                    y: overlay.py(faceItem.modelData.box.y)
                }

                // The five landmarks, in YuNet's order. The colours are only
                // there to make a transposed or permuted set obvious at a
                // glance, which is exactly how the last one went unnoticed.
                Repeater {
                    model: faceItem.modelData.landmarks

                    delegate: Rectangle {
                        required property int index
                        required property var modelData

                        readonly property var landmarkColors: ["#ff5252", "#448aff", "#69f0ae", "#ffab40", "#e040fb"]

                        color: landmarkColors[index] || "white"
                        height: overlay.dotSize
                        radius: height / 2
                        width: overlay.dotSize
                        x: overlay.px(modelData.x) - width / 2
                        y: overlay.py(modelData.y) - height / 2
                    }
                }

                Rectangle {
                    id: label

                    color: "#c8000000"
                    height: labelColumn.height + 6
                    radius: 3
                    visible: overlay.showLabels
                    width: labelColumn.width + 10
                    x: Math.max(0, Math.min(overlay.width - width, faceBox.x))
                    y: faceBox.y > height + 4 ? faceBox.y - height - 4 : faceBox.y + faceBox.height + 4

                    Column {
                        id: labelColumn

                        anchors.centerIn: parent
                        spacing: 1

                        Text {
                            color: faceItem.tint
                            font.bold: true
                            font.pixelSize: overlay.labelSize + 1
                            text: faceItem.modelData.known ? (faceItem.modelData.identity + " " + Math.round(faceItem.modelData.identityConfidence * 100) + "%") : "unknown"
                        }

                        Text {
                            color: "#e0e0e0"
                            font.pixelSize: overlay.labelSize
                            text: faceItem.modelData.emotion
                            visible: text !== ""
                        }

                        Text {
                            color: "#9e9e9e"
                            font.pixelSize: overlay.labelSize
                            text: faceItem.modelData.gaze
                            visible: text !== ""
                        }

                        // The rank the manifest gives this person. Shown
                        // because the eyes following one face out of three is
                        // only obviously correct once the reason is on screen.
                        Text {
                            color: faceItem.modelData.priority === 0 ? "#ffd740" : "#9e9e9e"
                            font.pixelSize: overlay.labelSize
                            text: "rank " + faceItem.modelData.priority
                            visible: faceItem.modelData.ranked
                        }
                    }
                }

                // What this person just said, over their head.
                //
                // Inside the face delegate rather than positioned from
                // outside, because being above somebody's head means knowing
                // where their head is and the delegate is already the thing
                // that knows. The match is on the name: the face gallery and
                // the voice gallery are enrolled from one manifest, so the two
                // halves agree on the string when they agree on the person.
                //
                // Nothing is drawn for a voice with no face in shot. That is
                // not a gap: the transcript is in the hearing panel either
                // way, and a caption floating over a stranger's head or over
                // nobody at all would be a claim rather than an observation.
                Rectangle {
                    id: bubble

                    readonly property bool mine: faceItem.modelData.known
                                                 && overlay.speechText !== ""
                                                 && faceItem.modelData.identity === overlay.speechSpeaker
                    readonly property bool labelAbove: label.visible && label.y < faceBox.y
                    readonly property real headTop: bubble.labelAbove ? label.y : faceBox.y
                    readonly property real headBottom: Math.max(faceBox.y + faceBox.height,
                                                                label.visible && label.y > faceBox.y ? label.y + label.height : 0)

                    antialiasing: true
                    border.color: faceItem.tint
                    border.width: 1
                    color: "#e6101418"
                    height: bubbleText.height + 10
                    opacity: bubble.mine && overlay.speechShown ? 1 : 0
                    radius: 4
                    visible: opacity > 0
                    width: bubbleText.width + 14

                    // Above the head when there is room above it, below
                    // everything when there is not. A person standing close
                    // enough fills the top of the frame, and a bubble clamped
                    // to y zero sits across their forehead.
                    y: bubble.headTop - height - 4 >= 0 ? bubble.headTop - height - 4 : bubble.headBottom + 4
                    x: Math.max(0, Math.min(overlay.width - width,
                                            faceBox.x + ((faceBox.width - width) / 2)))

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 180
                        }
                    }

                    Text {
                        id: bubbleText

                        anchors.centerIn: parent
                        color: "#eceff4"
                        elide: Text.ElideRight
                        font.pixelSize: overlay.labelSize + 1
                        horizontalAlignment: Text.AlignHCenter
                        // Three lines at most, and the width is capped before
                        // the wrap so that a long sentence grows downwards
                        // instead of across the whole frame.
                        maximumLineCount: 3
                        text: overlay.speechText
                        width: Math.min(implicitWidth, Math.min(overlay.width * 0.72, 320))
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    border.color: "#3c414a"
    border.width: 1
    color: "#15171a"

    CameraFrameView {
        id: cameraView

        anchors.fill: parent
        anchors.margins: 2
        frame: root.faceTracker.cameraFrame

        PerceptionOverlay {
            showLabels: false
            speechShown: root.speechShown
            speechSpeaker: root.speechSpeaker
            speechText: root.speechText
            tracker: root.faceTracker
            view: cameraView
        }

        MouseArea {
            id: popupMouseArea

            acceptedButtons: Qt.LeftButton
            anchors.fill: parent
            // On whether there is a picture, which changes when the camera
            // starts and when it stops. It used to be on an Image's load
            // status, which cycled several times a second, so the click that
            // opens the popup landed on a disabled target as often as not.
            enabled: root.faceTracker && root.faceTracker.enabled && cameraView.hasFrame
            hoverEnabled: true
            // The panel is a Flickable and this is a click inside it. Without
            // this the press is handed to the flick as soon as the pointer
            // moves a few pixels, which is the other half of why the popup
            // sometimes refused to open.
            preventStealing: true

            onClicked: mouse => {
                mouse.accepted = true;
                popup.open();
            }

            Rectangle {
                id: rectPopupHover

                anchors.fill: popupMouseArea
                anchors.margins: 2
                border.color: "white"
                border.width: 1
                color: "transparent"
                opacity: popupMouseArea.containsMouse && popupMouseArea.enabled ? 0.3 : 0
                visible: popupMouseArea.enabled

                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                    }
                }
            }
        }
    }

    // The primary face, named under the feed, so the panel says who it is
    // without the popup having to be opened.
    Text {
        // Four states and no fifth. This line used to report the preview
        // image's load status as well, which cycled between loading and ready
        // several times a second and made the panel look like it was fighting
        // the camera. There is no load any more: either a frame has arrived or
        // one has not.
        function statusColor() {
            if (!root.faceTracker || !root.faceTracker.enabled)
                return "gray";
            if (root.faceTracker.errorString !== "")
                return "red";
            if (!cameraView.hasFrame)
                return "orange";
            if (root.faceTracker.faceDetected)
                return root.identityColor(root.faceTracker.faces[0]);
            return "cyan";
        }

        function statusText() {
            if (!root.faceTracker)
                return "No Tracker";
            if (!root.faceTracker.enabled)
                return "Disabled";
            if (root.faceTracker.errorString !== "")
                return "Error: " + root.faceTracker.errorString;
            if (!cameraView.hasFrame)
                return "No Signal";
            if (!root.faceTracker.faceDetected)
                return "Searching...";
            const name = root.faceTracker.identity !== "" ? root.faceTracker.identity : "Unknown face";
            const mood = root.faceTracker.emotion !== "" ? " - " + root.faceTracker.emotion : "";
            return name + mood;
        }

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.bottom
        anchors.topMargin: 4
        color: statusColor()
        elide: Text.ElideRight
        font.pixelSize: 9
        text: statusText()
    }

    Popup {
        id: popup

        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        height: Overlay.overlay ? Overlay.overlay.height * 0.85 : 400
        modal: true
        width: Overlay.overlay ? Overlay.overlay.width * 0.8 : 600

        Overlay.modal: Rectangle {
            color: "#80000000"
        }
        background: Rectangle {
            border.color: "#666"
            border.width: 2
            color: "#1a1a1a"
            radius: 8
        }

        Column {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 12

            Item {
                height: 30
                width: parent.width

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    font.bold: true
                    font.pixelSize: 16
                    text: "Camera Feed"
                }

                Button {
                    id: closeButton

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    font.bold: true
                    font.pixelSize: 16
                    height: 30
                    text: "X"
                    width: 30

                    background: Rectangle {
                        color: closeButton.hovered ? "#ff4444" : "#666666"
                        opacity: 0.8
                        radius: 15
                    }
                    contentItem: Text {
                        color: "white"
                        font: closeButton.font
                        horizontalAlignment: Text.AlignHCenter
                        text: closeButton.text
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: popup.close()
                }
            }

            Rectangle {
                border.color: "#444"
                border.width: 1
                color: "#000"
                height: parent.height - controlsPanel.height - parent.spacing * 2 - 42
                width: parent.width

                CameraFrameView {
                    id: popupView

                    anchors.fill: parent
                    anchors.margins: 1
                    frame: root.faceTracker.cameraFrame

                    PerceptionOverlay {
                        dotSize: 8
                        labelSize: 12
                        lineWidth: 3
                        speechShown: root.speechShown
                        speechSpeaker: root.speechSpeaker
                        speechText: root.speechText
                        tracker: root.faceTracker
                        view: popupView
                    }
                }
            }

            Rectangle {
                id: controlsPanel

                color: "#c8000000"
                height: 210
                radius: 6
                width: parent.width

                Grid {
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Detection Confidence:"
                        width: 130
                    }

                    Row {
                        spacing: 10

                        Slider {
                            id: confidenceSlider

                            from: 0.3
                            height: 22
                            to: 0.9
                            value: root.faceTracker ? root.faceTracker.detectionConfidence : 0.6
                            width: 150

                            onMoved: {
                                if (root.faceTracker)
                                    root.faceTracker.detectionConfidence = value;
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "lightgray"
                            font.pixelSize: 10
                            text: confidenceSlider.value.toFixed(2)
                        }
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Tracking Smoothing:"
                    }

                    Row {
                        spacing: 10

                        Slider {
                            id: smoothingSlider

                            from: 0.0
                            height: 22
                            to: 1.0
                            value: root.faceTracker ? root.faceTracker.trackingSmoothing : 0.7
                            width: 150

                            onMoved: {
                                if (root.faceTracker)
                                    root.faceTracker.trackingSmoothing = value;
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "lightgray"
                            font.pixelSize: 10
                            text: smoothingSlider.value.toFixed(2)
                        }
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Camera:"
                    }

                    Text {
                        color: "lightgray"
                        font.pixelSize: 11
                        text: root.faceTracker ? (root.faceTracker.currentResolution + " delivered, " + root.faceTracker.requestedResolution + " requested") : "Unknown"
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Analysis:"
                    }

                    Text {
                        color: "lightgray"
                        font.pixelSize: 11
                        text: root.faceTracker ? (root.faceTracker.analysisResolution + " in " + root.faceTracker.analysisMs + " ms" + (root.faceTracker.poseReady ? ", pose on" : ", pose off") + (root.faceTracker.emotionReady ? ", expression on" : ", expression off")) : ""
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Gallery:"
                    }

                    Text {
                        color: "lightgray"
                        elide: Text.ElideMiddle
                        font.pixelSize: 11
                        text: {
                            if (!root.faceTracker)
                                return "";
                            if (!root.faceTracker.identityReady)
                                return "recognition model not provisioned";
                            if (root.faceTracker.knownIdentities.length === 0)
                                return "nobody enrolled";
                            return root.faceTracker.knownIdentities.join(", ") + "  (" + root.faceTracker.galleryDirectory + ")";
                        }
                        width: 320
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Roster:"
                    }

                    Text {
                        color: "lightgray"
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        text: {
                            if (!root.faceTracker || root.faceTracker.roster.length === 0)
                                return "no ids.json; enrolling by file name, everybody unranked";
                            return root.faceTracker.roster.join(", ");
                        }
                        width: 320
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Hands:"
                    }

                    Text {
                        color: "lightgray"
                        font.pixelSize: 11
                        text: {
                            if (!root.faceTracker)
                                return "";
                            if (!root.faceTracker.poseReady)
                                return "pose model not provisioned";
                            const hands = root.faceTracker.hands;
                            const held = root.faceTracker.heldLabels;
                            if (hands.length === 0)
                                return "none visible for a recognised person";
                            let seen = hands.length + " visible";
                            // Says which of the two qualities this is. "2
                            // visible" reads as two measured hands whether or
                            // not the models are there, and they are optional.
                            if (!root.faceTracker.handsReady)
                                seen += " (estimated; hand models not provisioned)";
                            else
                                seen += ", " + hands.filter(h => h.tracked).length + " tracked";
                            if (!root.faceTracker.objectsReady)
                                return seen + ", object model not provisioned";
                            return held.length === 0 ? seen + ", empty" : seen + ", holding " + held.join(", ");
                        }
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Head:"
                    }

                    Text {
                        color: "lightgray"
                        font.pixelSize: 11
                        text: {
                            if (!root.faceTracker || !root.faceTracker.faceDetected)
                                return "no face";
                            return "yaw " + root.faceTracker.headYaw.toFixed(0) + "deg, pitch " + root.faceTracker.headPitch.toFixed(0) + "deg, roll " + root.faceTracker.headRoll.toFixed(0) + "deg - " + root.faceTracker.gazeDirection;
                        }
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 11
                        text: "Status:"
                    }

                    Row {
                        spacing: 8

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.faceTracker && root.faceTracker.faceDetected ? "lime" : (root.faceTracker && root.faceTracker.enabled ? "orange" : "gray")
                            height: 12
                            radius: 6
                            width: 12
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "lightgray"
                            font.pixelSize: 11
                            text: {
                                if (!root.faceTracker || !root.faceTracker.enabled)
                                    return "Face tracking disabled";
                                if (root.faceTracker.errorString !== "")
                                    return "Error: " + root.faceTracker.errorString;
                                const people = root.faceTracker.people.length;
                                return root.faceTracker.faces.length + " face(s), " + people + " person(s)";
                            }
                        }
                    }
                }
            }
        }
    }
}
