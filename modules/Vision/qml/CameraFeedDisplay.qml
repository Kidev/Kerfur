pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import qml 1.0

Rectangle {
    id: root

    required property var faceTracker

    border.color: "#444"
    border.width: 1
    color: "#1a1a1a"

    // Main camera feed image
    Image {
        id: cameraImage

        readonly property real displayAspectRatio: width > 0 ? width / height : 1

        // Calculate scaling factors for overlay positioning
        readonly property real imageAspectRatio: sourceSize.width > 0 ? sourceSize.width / sourceSize.height : 1
        readonly property real offsetX: imageAspectRatio > displayAspectRatio ? (width - paintedWidth) / 2 : (width - sourceSize.width * scaleY * imageAspectRatio) / 2
        readonly property real offsetY: imageAspectRatio > displayAspectRatio ? (height - sourceSize.height * scaleX / imageAspectRatio) / 2 : (height - paintedHeight) / 2
        readonly property real scaleX: imageAspectRatio > displayAspectRatio ? paintedWidth / sourceSize.width : paintedHeight / sourceSize.height * imageAspectRatio
        readonly property real scaleY: imageAspectRatio > displayAspectRatio ? paintedWidth / sourceSize.width / imageAspectRatio : paintedHeight / sourceSize.height

        anchors.fill: parent
        anchors.margins: 2
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 : ""

        // Face detection overlay - ONLY the green rectangle
        Rectangle {
            id: faceOverlay

            border.color: "lime"
            border.width: 2
            color: "transparent"
            height: faceOverlay.visible ? root.faceTracker.facePixelSize.height * cameraImage.scaleY : 0
            visible: root.faceTracker && root.faceTracker.faceDetected && cameraImage.status === Image.Ready && cameraImage.sourceSize.width > 0 && cameraImage.paintedWidth > 0
            width: faceOverlay.visible ? root.faceTracker.facePixelSize.width * cameraImage.scaleX : 0
            x: faceOverlay.visible ? cameraImage.offsetX + root.faceTracker.facePixelCenter.x * cameraImage.scaleX - width / 2 : 0
            y: faceOverlay.visible ? cameraImage.offsetY + root.faceTracker.facePixelCenter.y * cameraImage.scaleY - height / 2 : 0

            Behavior on height {
                enabled: faceOverlay.visible

                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on width {
                enabled: faceOverlay.visible

                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutQuad
                }
            }

            // Smooth animations
            Behavior on x {
                enabled: faceOverlay.visible

                NumberAnimation {
                    duration: 250
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on y {
                enabled: faceOverlay.visible

                NumberAnimation {
                    duration: 250
                    easing.type: Easing.OutQuad
                }
            }

            // Center dot only
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 4
                radius: 2
                visible: faceOverlay.visible
                width: 4
            }
        }

        // Click area for popup - make it more reliable
        MouseArea {
            id: popupMouseArea

            acceptedButtons: Qt.LeftButton
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.faceTracker && root.faceTracker.enabled && cameraImage.status === Image.Ready

            onClicked: (mouse) => {
                mouse.accepted = true;
                popup.open();
            }

            // Visual feedback on hover
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                border.color: "white"
                border.width: 1
                color: "transparent"
                opacity: 0
                visible: popupMouseArea.enabled

                states: State {
                    name: "hovered"
                    when: popupMouseArea.containsMouse && popupMouseArea.enabled

                    PropertyChanges {
                        opacity: 0.3
                        target: parent
                    }
                }
                transitions: Transition {
                    NumberAnimation {
                        duration: 200
                        property: "opacity"
                    }
                }
            }
        }
    }

    // Status info below the camera feed
    Text {
        function getStatusColor() {
            if (!root.faceTracker || !root.faceTracker.enabled)
                return "gray";
            if (root.faceTracker.errorString !== "")
                return "red";
            if (cameraImage.status !== Image.Ready)
                return "orange";
            return root.faceTracker.faceDetected ? "lime" : "cyan";
        }

        function getStatusText() {
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
            return root.faceTracker.faceDetected ? "Face Detected" : "Searching...";
        }

        anchors.left: parent.left
        anchors.top: parent.bottom
        anchors.topMargin: 4
        color: getStatusColor()
        font.pixelSize: 9
        text: getStatusText()
    }

    // Camera feed popup - simplified without resolution controls
    Popup {
        id: popup

        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        height: Overlay.overlay ? Overlay.overlay.height * 0.8 : 400
        modal: true
        width: Overlay.overlay ? Overlay.overlay.width * 0.8 : 600

        // Overlay background dimming
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
            spacing: 15

            // Header
            Item {
                width: parent.width
                height: 30

                Text {
                    color: "white"
                    font.bold: true
                    font.pixelSize: 16
                    text: "Camera Feed"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                // Close button
                Button {
                    id: closeButton

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    font.bold: true
                    font.pixelSize: 16
                    height: 30
                    text: "✕"
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

            // Large camera image
            Rectangle {
                width: parent.width
                height: parent.height - controlsPanel.height - parent.spacing - 50
                color: "#000"
                border.color: "#444"
                border.width: 1

                Image {
                    id: popupImage

                    readonly property real popupOffsetX: (width - paintedWidth) / 2
                    readonly property real popupOffsetY: (height - paintedHeight) / 2
                    readonly property real popupScaleX: sourceSize.width > 0 ? paintedWidth / sourceSize.width : 1
                    readonly property real popupScaleY: sourceSize.height > 0 ? paintedHeight / sourceSize.height : 1

                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 : ""

                    // Face detection overlay for popup
                    Rectangle {
                        id: popupFaceOverlay

                        border.color: "lime"
                        border.width: 3
                        color: "transparent"
                        height: popupFaceOverlay.visible ? root.faceTracker.facePixelSize.height * popupImage.popupScaleY : 0
                        visible: root.faceTracker && root.faceTracker.faceDetected && popupImage.status === Image.Ready && popupImage.sourceSize.width > 0
                        width: popupFaceOverlay.visible ? root.faceTracker.facePixelSize.width * popupImage.popupScaleX : 0
                        x: popupFaceOverlay.visible ? popupImage.popupOffsetX + root.faceTracker.facePixelCenter.x * popupImage.popupScaleX - width / 2 : 0
                        y: popupFaceOverlay.visible ? popupImage.popupOffsetY + root.faceTracker.facePixelCenter.y * popupImage.popupScaleY - height / 2 : 0

                        // Center dot
                        Rectangle {
                            anchors.centerIn: parent
                            color: "red"
                            height: 8
                            radius: 4
                            visible: popupFaceOverlay.visible
                            width: 8
                        }

                        // Face label for popup
                        Rectangle {
                            anchors.bottom: parent.top
                            anchors.bottomMargin: 10
                            anchors.horizontalCenter: parent.horizontalCenter
                            color: "#C0000000"
                            height: faceLabel.height + 8
                            radius: 4
                            visible: popupFaceOverlay.visible
                            width: faceLabel.width + 16

                            Text {
                                id: faceLabel
                                anchors.centerIn: parent
                                color: "lime"
                                font.bold: true
                                font.pixelSize: 12
                                text: "FACE DETECTED"
                            }
                        }
                    }
                }
            }

            // Camera controls panel - simplified
            Rectangle {
                id: controlsPanel

                color: "#C0000000"
                height: 120
                radius: 6
                width: parent.width

                Column {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 12

                    // Detection Confidence
                    Row {
                        spacing: 15
                        width: parent.width

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            font.pixelSize: 11
                            text: "Detection Confidence:"
                            width: 120
                        }

                        Slider {
                            id: confidenceSlider

                            from: 0.3
                            height: 25
                            to: 0.9
                            value: root.faceTracker ? root.faceTracker.detectionConfidence : 0.7
                            width: 150

                            onValueChanged: {
                                if (root.faceTracker) {
                                    root.faceTracker.detectionConfidence = value;
                                }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "lightgray"
                            font.pixelSize: 10
                            text: confidenceSlider.value.toFixed(2)
                        }
                    }

                    // Tracking Smoothing
                    Row {
                        spacing: 15
                        width: parent.width

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            font.pixelSize: 11
                            text: "Tracking Smoothing:"
                            width: 120
                        }

                        Slider {
                            id: smoothingSlider

                            from: 0.0
                            height: 25
                            to: 1.0
                            value: root.faceTracker ? root.faceTracker.trackingSmoothing : 0.3
                            width: 150

                            onValueChanged: {
                                if (root.faceTracker) {
                                    root.faceTracker.trackingSmoothing = value;
                                }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "lightgray"
                            font.pixelSize: 10
                            text: smoothingSlider.value.toFixed(2)
                        }
                    }

                    // Status info
                    Row {
                        spacing: 15
                        width: parent.width

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            font.pixelSize: 11
                            text: "Status:"
                            width: 120
                        }

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
                                return root.faceTracker.faceDetected ? "Face detected and tracking" : "Searching for face...";
                            }
                        }
                    }
                }
            }
        }
    }
}
