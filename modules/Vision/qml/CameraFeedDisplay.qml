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
        readonly property real displayOffsetX: (cameraImage.width - cameraImage.paintedWidth) / 2
        readonly property real displayOffsetY: (cameraImage.height - cameraImage.paintedHeight) / 2
        readonly property real displayScaleX: cameraImage.paintedWidth / cameraImage.sourceSize.width
        readonly property real displayScaleY: cameraImage.paintedHeight / cameraImage.sourceSize.height

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

        // Face detection overlay - green rectangle
        Rectangle {
            id: faceOverlay

            readonly property bool isValidFaceData: root.faceTracker && root.faceTracker.faceDetected && cameraImage.status === Image.Ready && cameraImage.sourceSize.width > 0 && cameraImage.paintedWidth > 0 && root.faceTracker.facePixelSize.width > 0 && root.faceTracker.facePixelSize.height > 0

            border.color: "lime"
            border.width: 2
            color: "transparent"
            height: root.faceTracker.facePixelSize.height * cameraImage.displayScaleY
            visible: isValidFaceData
            width: root.faceTracker.facePixelSize.width * cameraImage.displayScaleX
            x: cameraImage.displayOffsetX + (root.faceTracker.facePixelCenter.x - root.faceTracker.facePixelSize.width / 2) * cameraImage.displayScaleX
            y: cameraImage.displayOffsetY + (root.faceTracker.facePixelCenter.y - root.faceTracker.facePixelSize.height / 2) * cameraImage.displayScaleY

            // Disable animations to prevent flickering issues

            // Center dot
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 4
                radius: 2
                visible: faceOverlay.visible
                width: 4
            }
        }

        // Face landmarks - using same coordinate system as face rectangle
        Repeater {
            model: root.faceTracker ? root.faceTracker.faceLandmarks : []

            delegate: Rectangle {
                required property int index
                readonly property bool isValidLandmark: modelData && typeof modelData.x !== "undefined" && typeof modelData.y !== "undefined" && cameraImage.status === Image.Ready && cameraImage.sourceSize.width > 0 && cameraImage.paintedWidth > 0 && root.faceTracker && root.faceTracker.faceDetected
                readonly property var landmarkColors: ["red", "blue", "lime", "magenta", "cyan"]
                required property var modelData

                color: landmarkColors[index] || "white"
                height: 6
                parent: cameraImage
                radius: 3
                visible: isValidLandmark
                width: 6

                // Position landmarks using same coordinate system as face rectangle
                x: isValidLandmark ? cameraImage.displayOffsetX + modelData.x * cameraImage.displayScaleX - width / 2 : -100
                y: isValidLandmark ? cameraImage.displayOffsetY + modelData.y * cameraImage.displayScaleY - height / 2 : -100
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

                        readonly property bool isValidPopupFace: root.faceTracker && root.faceTracker.faceDetected && popupImage.status === Image.Ready && popupImage.sourceSize.width > 0 && root.faceTracker.facePixelSize.width > 0 && root.faceTracker.facePixelSize.height > 0

                        border.color: "lime"
                        border.width: 3
                        color: "transparent"
                        height: isValidPopupFace ? root.faceTracker.facePixelSize.height * popupImage.popupScaleY : 20
                        visible: isValidPopupFace
                        width: isValidPopupFace ? root.faceTracker.facePixelSize.width * popupImage.popupScaleX : 20
                        x: isValidPopupFace ? popupImage.popupOffsetX + (root.faceTracker.facePixelCenter.x - root.faceTracker.facePixelSize.width / 2) * popupImage.popupScaleX : 0
                        y: isValidPopupFace ? popupImage.popupOffsetY + (root.faceTracker.facePixelCenter.y - root.faceTracker.facePixelSize.height / 2) * popupImage.popupScaleY : 0

                        // Center dot
                        Rectangle {
                            anchors.centerIn: parent
                            color: "red"
                            height: 8
                            radius: 4
                            width: 8
                        }
                    }

                    // Face landmarks for popup - using same coordinate system as popup face rectangle
                    Repeater {
                        model: root.faceTracker ? root.faceTracker.faceLandmarks : []

                        delegate: Rectangle {
                            required property int index
                            readonly property bool isValidPopupLandmark: modelData && typeof modelData.x !== "undefined" && typeof modelData.y !== "undefined" && popupImage.status === Image.Ready && popupImage.sourceSize.width > 0 && popupImage.paintedWidth > 0 && root.faceTracker && root.faceTracker.faceDetected
                            readonly property var landmarkColors: ["red", "blue", "lime", "magenta", "cyan"]
                            required property var modelData

                            color: landmarkColors[index] || "white"
                            height: 8
                            parent: popupImage
                            radius: 4
                            visible: isValidPopupLandmark
                            width: 8

                            // Position landmarks using same coordinate system as popup face rectangle
                            x: isValidPopupLandmark ? popupImage.popupOffsetX + modelData.x * popupImage.popupScaleX - width / 2 : -100
                            y: isValidPopupLandmark ? popupImage.popupOffsetY + modelData.y * popupImage.popupScaleY - height / 2 : -100
                        }
                    }

                    // Face label for popup - positioned relative to popup face overlay
                    Rectangle {
                        anchors.bottom: popupFaceOverlay.visible ? popupFaceOverlay.top : undefined
                        anchors.bottomMargin: popupFaceOverlay.visible ? 10 : 0
                        anchors.horizontalCenter: popupFaceOverlay.visible ? popupFaceOverlay.horizontalCenter : undefined
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
