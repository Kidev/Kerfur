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

        property int debugCounter: 0
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

        // Direct connection to the base64 property
        source: root.faceTracker && root.faceTracker.enabled ? root.faceTracker.cameraFrameBase64 :
                                                               ""

        onStatusChanged: {
            if (status === Image.Error) {
                console.error("CameraFeedDisplay: Failed to load image");
            } else if (status === Image.Ready) {
                // Image loaded successfully
                if (debugCounter < 3) {
                    //console.log("CameraFeedDisplay: Image loaded successfully, size:",
                    //          paintedWidth + "x" + paintedHeight);
                    debugCounter++;
                }
            }
        }

        // Face detection overlay
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

            // Position and size based on face detection data
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

            // Smooth animations for face tracking
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

            // Center dot
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 4
                radius: 2
                visible: parent.visible
                width: 4
            }

            // Face label
            Text {
                anchors.bottom: parent.top
                anchors.bottomMargin: 5
                anchors.horizontalCenter: parent.horizontalCenter
                color: "lime"
                font.bold: true
                font.pixelSize: 9
                text: "FACE"
                visible: parent.visible

                Rectangle {
                    anchors.centerIn: parent
                    anchors.margins: -2
                    color: "#80000000"
                    height: parent.height + 4
                    radius: 2
                    width: parent.width + 4
                    z: -1
                }
            }
        }

        // Center crosshair for reference
        Item {
            anchors.centerIn: parent
            visible: root.faceTracker && root.faceTracker.enabled && cameraImage.status
                     === Image.Ready

            Rectangle {
                anchors.centerIn: parent
                color: "white"
                height: 1
                opacity: 0.6
                width: 16
            }

            Rectangle {
                anchors.centerIn: parent
                color: "white"
                height: 16
                opacity: 0.6
                width: 1
            }
        }
    }

    // Status overlay
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: 4
        color: "#C0000000"
        height: statusText.height + 4
        radius: 3
        visible: statusText.text !== ""
        width: statusText.width + 8

        Text {
            id: statusText

            function getStatusColor() {
                if (!root.faceTracker)
                    return "gray";
                if (!root.faceTracker.enabled)
                    return "orange";
                if (root.faceTracker.errorString !== "")
                    return "red";
                if (cameraImage.status === Image.Loading)
                    return "yellow";
                if (cameraImage.status === Image.Error)
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
                    return "Error";
                if (cameraImage.status === Image.Loading)
                    return "Loading...";
                if (cameraImage.status === Image.Error)
                    return "Image Error";
                if (cameraImage.status !== Image.Ready)
                    return "No Signal";
                return root.faceTracker.faceDetected ? "Face Detected" : "Searching...";
            }

            anchors.centerIn: parent
            color: getStatusColor()
            font.bold: true
            font.pixelSize: 9
            text: getStatusText()
        }
    }

    // Debug info overlay
    Rectangle {
        anchors.left: parent.left
        anchors.margins: 4
        anchors.top: parent.top
        color: "#80000000"
        height: debugText.height + 4
        radius: 3
        visible: root.faceTracker && root.faceTracker.enabled && debugText.text !== ""
        width: debugText.width + 8

        Text {
            id: debugText

            anchors.centerIn: parent
            color: "cyan"
            font.pixelSize: 8
            text: {
                if (!root.faceTracker)
                    return "";
                var info = "Status: " + cameraImage.status;
                if (cameraImage.sourceSize.width > 0) {
                    info += "\nSize: " + cameraImage.sourceSize.width + "×"
                            + cameraImage.sourceSize.height;
                    info += "\nPainted: " + Math.round(cameraImage.paintedWidth) + "×" + Math.round(
                                cameraImage.paintedHeight);
                }
                if (root.faceTracker.faceDetected) {
                    var center = root.faceTracker.faceCenter;
                    info += "\nFace: " + (center.x * 100).toFixed(0) + "%, " + (center.y
                                                                                * 100).toFixed(0)
                            + "%";
                }
                return info;
            }
        }
    }

    // Error display
    Rectangle {
        anchors.centerIn: parent
        color: "#C0800000"
        height: errorText.implicitHeight + 20
        radius: 5
        visible: root.faceTracker && root.faceTracker.errorString !== ""
        width: Math.min(parent.width - 20, errorText.implicitWidth + 20)

        Text {
            id: errorText

            anchors.centerIn: parent
            color: "white"
            font.bold: true
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            text: root.faceTracker ? root.faceTracker.errorString : ""
            width: Math.min(root.width - 40, implicitWidth)
            wrapMode: Text.WordWrap
        }
    }

    // No camera message
    Rectangle {
        function shouldShowNoFeed() {
            return !root.faceTracker || !root.faceTracker.enabled || (root.faceTracker.enabled && root.faceTracker.errorString
                                                                      === "" && cameraImage.status
                                                                      !== Image.Ready
                                                                      && cameraImage.status
                                                                      !== Image.Loading);
        }

        anchors.centerIn: parent
        color: "#80404040"
        height: noFeedText.height + 16
        radius: 8
        visible: shouldShowNoFeed()
        width: noFeedText.width + 16

        Text {
            id: noFeedText

            anchors.centerIn: parent
            color: "lightgray"
            font.bold: true
            font.pixelSize: 12
            text: !root.faceTracker ? "No Tracker" : !root.faceTracker.enabled ? "Camera Disabled" :
                                                                                 "No Camera Feed"
        }
    }

    // Force refresh connection
    Connections {
        function onCameraFrameChanged() {
            // The source binding should automatically update
            // but we can add debug logging here
            if (cameraImage.debugCounter < 5)
                //console.log("CameraFeedDisplay: Frame changed, source length:",
                //root.faceTracker.cameraFrameBase64.length);
            {}
        }

        target: root.faceTracker
    }
}
