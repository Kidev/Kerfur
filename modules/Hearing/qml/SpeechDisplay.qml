import QtQuick
import QtQuick.Controls
import Hearing

// Make sure all braces are properly closed
Rectangle {
    id: root

    required property var speechRecognizer  // Changed from SpeechRecognizer type

    color: "#2a2a2a"
    border.color: "#444"
    border.width: 1
    radius: 4

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // Volume indicator
        Row {
            width: parent.width
            spacing: 4

            Text {
                color: "white"
                font.pixelSize: 10
                text: "Audio:"
                width: 40
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: parent.width - 50
                height: 8
                color: "#1a1a1a"
                border.color: "#444"
                border.width: 1
                radius: 4
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width * (root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0)
                    height: parent.height
                    color: root.speechRecognizer && root.speechRecognizer.volumeLevel > 0.7 ? "red" :
                    root.speechRecognizer && root.speechRecognizer.volumeLevel > 0.4 ? "yellow" : "green"
                    radius: 4

                    Behavior on width {
                        NumberAnimation { duration: 50 }
                    }
                }
            }
        }

        // Status indicator
        Row {
            spacing: 6

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: root.speechRecognizer && root.speechRecognizer.listening ? "lime" :
                root.speechRecognizer && root.speechRecognizer.enabled ? "orange" : "gray"
                anchors.verticalCenter: parent.verticalCenter

                SequentialAnimation on opacity {
                    running: root.speechRecognizer && root.speechRecognizer.listening
                    loops: Animation.Infinite

                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }

            Text {
                color: "white"
                font.pixelSize: 10
                text: root.speechRecognizer ?
                (root.speechRecognizer.listening ? "Listening..." :
                root.speechRecognizer.enabled ? "Initializing..." : "Disabled") : "No Recognizer"
            }

            // Kerfur detection indicator
            Rectangle {
                visible: root.speechRecognizer && root.speechRecognizer.kerfurDetected
                width: 60
                height: 16
                color: "magenta"
                radius: 8

                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 9
                    font.bold: true
                    text: "KERFUR!"
                }

                SequentialAnimation on scale {
                    running: parent.visible

                    NumberAnimation { to: 1.2; duration: 100 }
                    NumberAnimation { to: 1.0; duration: 100 }
                }
            }
        }

        // Partial text
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.partialText !== ""
            width: parent.width
            color: "#aaaaaa"
            font.pixelSize: 9
            font.italic: true
            text: root.speechRecognizer ? ("..." + root.speechRecognizer.partialText) : ""
            wrapMode: Text.WordWrap
        }

        // Last recognized text
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.lastText !== ""
            width: parent.width
            color: "white"
            font.pixelSize: 10
            text: root.speechRecognizer ? root.speechRecognizer.lastText : ""
            wrapMode: Text.WordWrap
        }

        // Confidence
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.confidence > 0
            color: root.speechRecognizer && root.speechRecognizer.confidence > 0.7 ? "lime" :
            root.speechRecognizer && root.speechRecognizer.confidence > 0.4 ? "yellow" : "orange"
            font.pixelSize: 9
            text: root.speechRecognizer ? ("Confidence: " + (root.speechRecognizer.confidence * 100).toFixed(0) + "%") : ""
        }

        // Error message
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.errorString !== ""
            width: parent.width
            color: "red"
            font.pixelSize: 9
            text: root.speechRecognizer ? ("Error: " + root.speechRecognizer.errorString) : ""
            wrapMode: Text.WordWrap
        }
    }
}
