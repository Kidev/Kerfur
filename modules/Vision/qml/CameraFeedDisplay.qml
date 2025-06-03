import QtQuick
import QtQuick.Controls

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
        readonly property real imageAspectRatio: sourceSize.width > 0 ? sourceSize.width
                                                                        / sourceSize.height : 1
        readonly property real offsetX: imageAspectRatio > displayAspectRatio ? (width
                                                                                 - paintedWidth)
                                                                                / 2 : (width
                                                                                       - sourceSize.width
                                                                                       * scaleY
                                                                                       * imageAspectRatio)
                                                                                / 2
        readonly property real offsetY: imageAspectRatio > displayAspectRatio ? (height
                                                                                 - sourceSize.height
                                                                                 * scaleX
                                                                                 / imageAspectRatio)
                                                                                / 2 : (height
                                                                                       - paintedHeight)
                                                                                / 2
        readonly property real scaleX: imageAspectRatio > displayAspectRatio ? paintedWidth
                                                                               / sourceSize.width :
                                                                               paintedHeight
                                                                               / sourceSize.height
                                                                               * imageAspectRatio
        readonly property real scaleY: imageAspectRatio > displayAspectRatio ? paintedWidth
                                                                               / sourceSize.width
                                                                               / imageAspectRatio :
                                                                               paintedHeight
                                                                               / sourceSize.height

        anchors.fill: parent
        anchors.margins: 2
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 :
                                                               ""

        // Face detection overlay - ONLY the green rectangle
        Rectangle {
            id: faceOverlay

            border.color: "lime"
            border.width: 2
            color: "transparent"
            height: visible ? root.faceTracker.facePixelSize.height * cameraImage.scaleY : 0
            visible: root.faceTracker && root.faceTracker.faceDetected && cameraImage.status
                     === Image.Ready && cameraImage.sourceSize.width > 0
                     && cameraImage.paintedWidth > 0
            width: visible ? root.faceTracker.facePixelSize.width * cameraImage.scaleX : 0
            x: visible ? cameraImage.offsetX + root.faceTracker.facePixelCenter.x
                         * cameraImage.scaleX - width / 2 : 0
            y: visible ? cameraImage.offsetY + root.faceTracker.facePixelCenter.y
                         * cameraImage.scaleY - height / 2 : 0

            Behavior on height {
                enabled: visible

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on width {
                enabled: visible

                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }

            // Smooth animations
            Behavior on x {
                enabled: visible

                NumberAnimation {
                    duration: 150
                    easing.type: Easing.OutQuad
                }
            }
            Behavior on y {
                enabled: visible

                NumberAnimation {
                    duration: 150
                    easing.type: Easing.OutQuad
                }
            }

            // Center dot only
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 3
                radius: 1.5
                visible: parent.visible
                width: 3
            }
        }

        // Click area for popup
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: {
                if (root.faceTracker && root.faceTracker.enabled && cameraImage.status
                        === Image.Ready) {
                    popup.open();
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

    // Camera feed popup
    Popup {
        id: popup

        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        height: Overlay.overlay ? Overlay.overlay.height * 0.75 : 300
        modal: true
        width: Overlay.overlay ? Overlay.overlay.width * 0.75 : 400

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

        Item {
            anchors.fill: parent
            anchors.margins: 10

            // Large camera image
            Image {
                id: popupImage

                readonly property real popupOffsetX: (width - paintedWidth) / 2
                readonly property real popupOffsetY: (height - paintedHeight) / 2

                // Calculate scaling for popup
                readonly property real popupScaleX: sourceSize.width > 0 ? paintedWidth
                                                                           / sourceSize.width : 1
                readonly property real popupScaleY: sourceSize.height > 0 ? paintedHeight
                                                                            / sourceSize.height : 1

                anchors.fill: parent
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: root.faceTracker && root.faceTracker.enabled
                        ? root.faceTracker.cameraFrameBase64 : ""

                // Face detection overlay for popup
                Rectangle {
                    border.color: "lime"
                    border.width: 3
                    color: "transparent"
                    height: visible ? root.faceTracker.facePixelSize.height
                                      * popupImage.popupScaleY : 0
                    visible: root.faceTracker && root.faceTracker.faceDetected && popupImage.status
                             === Image.Ready && popupImage.sourceSize.width > 0
                    width: visible ? root.faceTracker.facePixelSize.width * popupImage.popupScaleX :
                                     0
                    x: visible ? popupImage.popupOffsetX + root.faceTracker.facePixelCenter.x
                                 * popupImage.popupScaleX - width / 2 : 0
                    y: visible ? popupImage.popupOffsetY + root.faceTracker.facePixelCenter.y
                                 * popupImage.popupScaleY - height / 2 : 0

                    // Center dot
                    Rectangle {
                        anchors.centerIn: parent
                        color: "red"
                        height: 6
                        radius: 3
                        visible: parent.visible
                        width: 6
                    }

                    // Face label for popup
                    Text {
                        anchors.bottom: parent.top
                        anchors.bottomMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "lime"
                        font.bold: true
                        font.pixelSize: 12
                        text: "DETECTED FACE"
                        visible: parent.visible

                        Rectangle {
                            anchors.centerIn: parent
                            anchors.margins: -4
                            color: "#C0000000"
                            height: parent.height + 8
                            radius: 4
                            width: parent.width + 8
                            z: -1
                        }
                    }
                }
            }

            // Info panel in popup
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                color: "#C0000000"
                height: infoText.height + 16
                radius: 6

                Text {
                    id: infoText

                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 11
                    text: {
                        var info = "";
                        if (root.faceTracker) {
                            info += "Resolution: " + root.faceTracker.currentResolution + " • ";
                            info += "Status: " + (root.faceTracker.faceDetected ? "Face Detected" :
                                                                                  "Searching")
                                    + " • ";
                            if (popupImage.sourceSize.width > 0) {
                                info += "Display: " + popupImage.sourceSize.width + "×"
                                        + popupImage.sourceSize.height;
                            }
                        }
                        return info;
                    }
                }
            }

            // Close button
            Button {
                anchors.right: parent.right
                anchors.top: parent.top
                font.bold: true
                font.pixelSize: 16
                height: 30
                text: "×"
                width: 30

                background: Rectangle {
                    color: parent.hovered ? "#ff4444" : "#666666"
                    radius: 15
                }
                contentItem: Text {
                    color: "white"
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: popup.close()
            }
        }
    }
}
