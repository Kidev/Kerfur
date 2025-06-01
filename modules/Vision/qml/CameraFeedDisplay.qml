import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var faceTracker

    border.color: "#444"
    border.width: 1
    color: "#1a1a1a"

    Image {
        id: cameraImage

        function updateImage() {
            // Force image refresh by changing source
            source = "";
            source = "data:image/png;base64," + Qt.btoa(root.faceTracker.cameraFrame);
        }

        anchors.fill: parent
        anchors.margins: 2
        fillMode: Image.PreserveAspectFit
        source: root.faceTracker && root.faceTracker.cameraFrame.width > 0 ? "image://provider/"
                                                                             + Date.now() : ""

        // Convert QImage to displayable format
        Component.onCompleted: {
            if (root.faceTracker) {
                root.faceTracker.cameraFrameChanged.connect(updateImage);
            }
        }

        // Face detection overlay
        Rectangle {
            id: faceOverlay

            border.color: root.faceTracker.faceDetected ? "lime" : "transparent"
            border.width: 2
            color: "transparent"
            height: root.faceTracker.facePixelSize.height
            radius: width / 2
            visible: root.faceTracker.faceDetected && cameraImage.status === Image.Ready
            width: root.faceTracker.facePixelSize.width
            x: root.faceTracker.facePixelCenter.x - width / 2
            y: root.faceTracker.facePixelCenter.y - height / 2

            // Center dot
            Rectangle {
                anchors.centerIn: parent
                color: "red"
                height: 6
                radius: 3
                width: 6
            }
        }
    }

    // Status text overlay
    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: 5
        color: root.faceTracker.faceDetected ? "lime" : "orange"
        font.pixelSize: 10
        text: root.faceTracker.faceDetected ? "Face Detected" : "No Face"
        visible: root.faceTracker.enabled
    }

    // Error text
    Text {
        anchors.centerIn: parent
        color: "red"
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        text: root.faceTracker.errorString
        visible: root.faceTracker.errorString !== ""
        width: parent.width - 10
        wrapMode: Text.WordWrap
    }
}
