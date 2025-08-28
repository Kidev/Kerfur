import QtQuick
import QtQuick.Controls
import Hearing

Rectangle {
    id: root

    required property var speechRecognizer

    color: "#2a2a2a"
    border.color: "#444"
    border.width: 1
    radius: 4

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Volume indicator
        Row {
            width: parent.width
            spacing: 6

            Text {
                color: "white"
                font.pixelSize: 10
                text: "Audio:"
                width: 40
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: parent.width - 50
                height: 12
                color: "#1a1a1a"
                border.color: "#444"
                border.width: 1
                radius: 6
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width * (root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0)
                    height: parent.height - 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: 1
                    color: {
                        if (!root.speechRecognizer) return "gray";
                        const level = root.speechRecognizer.volumeLevel;
                        if (level > 0.8) return "#ff4444";
                        if (level > 0.6) return "#ffaa44";
                        if (level > 0.3) return "#44ff44";
                        return "#44aa44";
                    }
                    radius: 4

                    Behavior on width {
                        NumberAnimation { duration: 100 }
                    }
                }

                // Volume level text
                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 8
                    text: root.speechRecognizer ?
                    (root.speechRecognizer.volumeLevel * 100).toFixed(0) + "%" : "0%"
                }
            }
        }

        // Status indicator row
        Row {
            spacing: 8
            width: parent.width

            // Status LED
            Rectangle {
                width: 12
                height: 12
                radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.speechRecognizer) return "gray";
                    if (!root.speechRecognizer.enabled) return "gray";
                    if (root.speechRecognizer.processing) return "yellow";
                    if (root.speechRecognizer.listening) return "lime";
                    return "orange";
                }

                SequentialAnimation on opacity {
                    running: root.speechRecognizer && root.speechRecognizer.listening && !root.speechRecognizer.processing
                    loops: Animation.Infinite

                    NumberAnimation { to: 0.3; duration: 800 }
                    NumberAnimation { to: 1.0; duration: 800 }
                }

                // Processing indicator
                RotationAnimation {
                    target: parent
                    running: root.speechRecognizer && root.speechRecognizer.processing
                    duration: 1000
                    loops: Animation.Infinite
                    from: 0
                    to: 360
                }
            }

            // Status text
            Text {
                color: "white"
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (!root.speechRecognizer) return "No Recognizer";
                    if (!root.speechRecognizer.enabled) return "Disabled";
                    if (root.speechRecognizer.errorString !== "") return "Error";
                    if (root.speechRecognizer.processing) return "Processing...";
                    if (root.speechRecognizer.listening) return "Listening...";
                    return "Initializing...";
                }
            }

            // Kerfur detection indicator
            Rectangle {
                visible: root.speechRecognizer && root.speechRecognizer.kerfurDetected
                width: 65
                height: 18
                color: "#ff44ff"
                radius: 9
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 10
                    font.bold: true
                    text: "KERFUR!"
                }

                SequentialAnimation on scale {
                    running: parent.visible
                    loops: 3

                    NumberAnimation { to: 1.3; duration: 150 }
                    NumberAnimation { to: 1.0; duration: 150 }
                }

                // Glow effect
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width + 4
                    height: parent.height + 4
                    color: "transparent"
                    border.color: "#ff44ff"
                    border.width: 2
                    radius: 11
                    opacity: 0.5

                    SequentialAnimation on opacity {
                        running: parent.parent.visible
                        loops: Animation.Infinite

                        NumberAnimation { to: 0.2; duration: 500 }
                        NumberAnimation { to: 0.8; duration: 500 }
                    }
                }
            }
        }

        // Separator line
        Rectangle {
            width: parent.width
            height: 1
            color: "#555"
            visible: root.speechRecognizer && (root.speechRecognizer.lastText !== "" || root.speechRecognizer.processing)
        }

        // Processing indicator
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.processing
            width: parent.width
            color: "#aaaaaa"
            font.pixelSize: 9
            font.italic: true
            text: "🎤 Processing audio..."

            SequentialAnimation on opacity {
                running: parent.visible
                loops: Animation.Infinite

                NumberAnimation { to: 0.5; duration: 500 }
                NumberAnimation { to: 1.0; duration: 500 }
            }
        }

        // Last recognized text
        Rectangle {
            visible: root.speechRecognizer && root.speechRecognizer.lastText !== ""
            width: parent.width
            height: textContent.height + 12
            color: "#3a3a3a"
            border.color: "#555"
            border.width: 1
            radius: 4

            Text {
                id: textContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                color: "white"
                font.pixelSize: 11
                text: root.speechRecognizer ? root.speechRecognizer.lastText : ""
                wrapMode: Text.WordWrap

                // Subtle animation when text changes
                Behavior on text {
                    SequentialAnimation {
                        NumberAnimation {
                            target: textContent
                            property: "opacity"
                            to: 0.7
                            duration: 100
                        }
                        NumberAnimation {
                            target: textContent
                            property: "opacity"
                            to: 1.0
                            duration: 200
                        }
                    }
                }
            }
        }

        // Confidence indicator
        Row {
            visible: root.speechRecognizer && root.speechRecognizer.confidence > 0
            spacing: 8

            Text {
                color: "#aaa"
                font.pixelSize: 9
                text: "Confidence:"
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: 60
                height: 8
                color: "#1a1a1a"
                border.color: "#444"
                border.width: 1
                radius: 4
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: parent.width * (root.speechRecognizer ? root.speechRecognizer.confidence : 0)
                    height: parent.height - 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: 1
                    color: {
                        if (!root.speechRecognizer) return "gray";
                        const conf = root.speechRecognizer.confidence;
                        if (conf > 0.8) return "lime";
                        if (conf > 0.6) return "yellow";
                        if (conf > 0.4) return "orange";
                        return "red";
                    }
                    radius: 3

                    Behavior on width {
                        NumberAnimation { duration: 300 }
                    }
                }
            }

            Text {
                color: {
                    if (!root.speechRecognizer) return "gray";
                    const conf = root.speechRecognizer.confidence;
                    if (conf > 0.7) return "lime";
                    if (conf > 0.4) return "yellow";
                    return "orange";
                }
                font.pixelSize: 9
                text: root.speechRecognizer ?
                (root.speechRecognizer.confidence * 100).toFixed(0) + "%" : "0%"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Error message
        Rectangle {
            visible: root.speechRecognizer && root.speechRecognizer.errorString !== ""
            width: parent.width
            height: errorText.height + 8
            color: "#4a2a2a"
            border.color: "#aa4444"
            border.width: 1
            radius: 4

            Text {
                id: errorText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                color: "#ffaaaa"
                font.pixelSize: 9
                text: root.speechRecognizer ? ("⚠ " + root.speechRecognizer.errorString) : ""
                wrapMode: Text.WordWrap
            }
        }

        // Debug info (only shown when there's useful information)
        Text {
            visible: root.speechRecognizer && root.speechRecognizer.enabled && root.speechRecognizer.errorString === ""
            width: parent.width
            color: "#666"
            font.pixelSize: 8
            text: {
                if (!root.speechRecognizer) return "";
                let info = [];
                if (root.speechRecognizer.listening) {
                    info.push("🎙 Listening");
                }
                if (root.speechRecognizer.processing) {
                    info.push("⚙ Processing");
                }
                if (root.speechRecognizer.volumeLevel > 0) {
                    info.push("📊 " + (root.speechRecognizer.volumeLevel * 100).toFixed(0) + "%");
                }
                return info.join(" • ");
            }
            wrapMode: Text.WordWrap
        }
    }
}
