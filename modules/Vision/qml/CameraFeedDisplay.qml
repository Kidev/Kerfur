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

        readonly property real displayOffsetX: (cameraImage.width - cameraImage.paintedWidth) / 2
        readonly property real displayOffsetY: (cameraImage.height - cameraImage.paintedHeight) / 2

        // Calculate proper scaling from source image to displayed image
        readonly property real displayScaleX: cameraImage.paintedWidth / cameraImage.sourceSize.width
        readonly property real displayScaleY: cameraImage.paintedHeight / cameraImage.sourceSize.height

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
            height: faceOverlay.visible ? root.faceTracker.facePixelSize.height * cameraImage.displayScaleY : 0
            visible: root.faceTracker && root.faceTracker.faceDetected && cameraImage.status === Image.Ready && cameraImage.sourceSize.width > 0 && cameraImage.paintedWidth > 0
            width: faceOverlay.visible ? root.faceTracker.facePixelSize.width * cameraImage.displayScaleX : 0
            x: faceOverlay.visible ? cameraImage.displayOffsetX + (root.faceTracker.facePixelCenter.x - root.faceTracker.facePixelSize.width / 2) * cameraImage.displayScaleX : 0
            y: faceOverlay.visible ? cameraImage.displayOffsetY + (root.faceTracker.facePixelCenter.y - root.faceTracker.facePixelSize.height / 2) * cameraImage.displayScaleY : 0

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

            // Center dot
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 4
                radius: 2
                visible: faceOverlay.visible
                width: 4
            }

            // Face landmarks - matching demo colors
            Repeater {
                model: root.faceTracker ? root.faceTracker.faceLandmarks : []

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    // Landmark colors matching demo: right eye (red), left eye (blue), nose (green), right mouth (magenta), left mouth (cyan)
                    readonly property var landmarkColors: ["red", "blue", "lime", "magenta", "cyan"]

                    color: landmarkColors[index] || "white"
                    height: 4
                    radius: 2
                    visible: faceOverlay.visible && modelData && typeof modelData.x !== "undefined" && typeof modelData.y !== "undefined"
                    width: 4
                    x: visible ? cameraImage.displayOffsetX + modelData.x * cameraImage.displayScaleX - width / 2 : 0
                    y: visible ? cameraImage.displayOffsetY + modelData.y * cameraImage.displayScaleY - height / 2 : 0
                }
            }
        }

        // Click area for popup - make it more reliable
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

            // Visual feedback on hover
            Rectangle {
                id: rectPopupHover
                anchors.fill: popupMouseArea
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
                        target: rectPopupHover
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
                border.color: "#444"
                border.width: 1
                color: "#000"
                height: parent.height - controlsPanel.height - parent.spacing - 50
                width: parent.width

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

                        // Face landmarks for popup
                        Repeater {
                            model: root.faceTracker ? root.faceTracker.faceLandmarks : []

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                // Landmark colors matching demo: right eye (red), left eye (blue), nose (green), right mouth (magenta), left mouth (cyan)
                                readonly property var landmarkColors: ["red", "blue", "lime", "magenta", "cyan"]

                                color: landmarkColors[index] || "white"
                                height: 6
                                radius: 3
                                visible: popupFaceOverlay.visible && modelData && typeof modelData.x !== "undefined" && typeof modelData.y !== "undefined"
                                width: 6
                                x: visible ? popupImage.popupOffsetX + modelData.x * popupImage.popupScaleX - width / 2 : 0
                                y: visible ? popupImage.popupOffsetY + modelData.y * popupImage.popupScaleY - height / 2 : 0
                            }
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
