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

        required property Image image
        required property var tracker
        property real dotSize: 5
        property real lineWidth: 2
        property bool showLabels: true
        property real labelSize: 9

        readonly property real originX: (image.width - image.paintedWidth) / 2
        readonly property real originY: (image.height - image.paintedHeight) / 2
        readonly property bool live: image.status === Image.Ready && image.paintedWidth > 0
                                     && tracker && tracker.enabled

        function px(nx) {
            return originX + nx * image.paintedWidth;
        }
        function py(ny) {
            return originY + ny * image.paintedHeight;
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

        anchors.fill: image
        visible: live

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
                    }
                }
            }
        }
    }

    border.color: "#444"
    border.width: 1
    color: "#1a1a1a"

    Image {
        id: cameraImage

        anchors.fill: parent
        anchors.margins: 2
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 : ""

        PerceptionOverlay {
            image: cameraImage
            showLabels: false
            tracker: root.faceTracker
        }

        MouseArea {
            id: popupMouseArea

            acceptedButtons: Qt.LeftButton
            anchors.fill: parent
            enabled: root.faceTracker && root.faceTracker.enabled && cameraImage.status === Image.Ready
            hoverEnabled: true

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
        function statusColor() {
            if (!root.faceTracker || !root.faceTracker.enabled)
                return "gray";
            if (root.faceTracker.errorString !== "")
                return "red";
            if (cameraImage.status !== Image.Ready)
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
            if (cameraImage.status === Image.Loading)
                return "Loading...";
            if (cameraImage.status === Image.Error)
                return "Image Error";
            if (cameraImage.status !== Image.Ready)
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

                Image {
                    id: popupImage

                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 : ""

                    PerceptionOverlay {
                        dotSize: 8
                        image: popupImage
                        labelSize: 12
                        lineWidth: 3
                        tracker: root.faceTracker
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
