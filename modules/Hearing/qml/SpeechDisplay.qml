import Hearing
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var speechRecognizer

    border.color: "#444"
    border.width: 1
    color: "#2a2a2a"
    radius: 4

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Volume indicator - improved version
        Row {
            spacing: 6
            width: parent.width

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                font.pixelSize: 10
                text: "Audio:"
                width: 40
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: "#444"
                border.width: 1
                color: "#1a1a1a"
                height: 16
                radius: 8
                width: parent.width - 90

                // Background grid for better visibility
                Repeater {
                    model: 10

                    Rectangle {
                        color: "#333"
                        height: parent.height - 4
                        opacity: 0.5
                        width: 1
                        x: 2 + index * (parent.width - 4) / 10
                        y: 2
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: {
                        if (!root.speechRecognizer || !root.speechRecognizer.listening)
                            return "gray";
                        const level = root.speechRecognizer.volumeLevel;
                        if (level > 0.8)
                            return "#ff4444";  // Red for too loud
                        if (level > 0.6)
                            return "#ffaa44";  // Orange for loud
                        if (level > 0.3)
                            return "#44ff44";  // Green for good
                        if (level > 0.1)
                            return "#44aa44";  // Dark green for quiet
                        return "#666666";                    // Gray for silence
                    }
                    height: parent.height - 2
                    radius: 6
                    width: parent.width * (root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0)
                    x: 1

                    Behavior on width {
                        NumberAnimation {
                            duration: 50
                        }  // Faster response
                    }
                }

                // Peak indicator
                Rectangle {
                    id: peakIndicator

                    property real peakLevel: 0

                    color: "yellow"
                    height: parent.height - 4
                    opacity: 0.8
                    visible: peakLevel > 0.1
                    width: 2
                    x: Math.min(parent.width - 3, 1 + parent.width * peakLevel)
                    y: 2

                    Timer {
                        interval: 100
                        repeat: true
                        running: root.speechRecognizer && root.speechRecognizer.listening

                        onTriggered: {
                            const currentLevel = root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0;
                            if (currentLevel > peakIndicator.peakLevel) {
                                peakIndicator.peakLevel = currentLevel;
                            } else {
                                peakIndicator.peakLevel = Math.max(0, peakIndicator.peakLevel - 0.02);
                            }
                        }
                    }
                }
            }

            // Volume percentage text
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.speechRecognizer || !root.speechRecognizer.listening)
                        return "gray";
                    const level = root.speechRecognizer.volumeLevel;
                    if (level > 0.8)
                        return "#ff4444";
                    if (level > 0.6)
                        return "#ffaa44";
                    if (level > 0.3)
                        return "#44ff44";
                    return "#666666";
                }
                font.pixelSize: 9
                text: root.speechRecognizer ? (root.speechRecognizer.volumeLevel * 100).toFixed(0) + "%" : "0%"
                width: 35
            }
        }

        // Status indicator row
        Row {
            spacing: 8
            width: parent.width

            // Status LED
            Rectangle {
                id: statusLed

                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.speechRecognizer)
                        return "gray";
                    if (!root.speechRecognizer.enabled)
                        return "gray";
                    if (root.speechRecognizer.processing)
                        return "yellow";
                    if (root.speechRecognizer.listening)
                        return "lime";
                    return "orange";
                }
                height: 12
                radius: 6
                width: 12

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: root.speechRecognizer && root.speechRecognizer.listening && !root.speechRecognizer.processing

                    NumberAnimation {
                        duration: 800
                        to: 0.3
                    }

                    NumberAnimation {
                        duration: 800
                        to: 1.0
                    }
                }

                // Processing indicator
                RotationAnimation {
                    duration: 1000
                    from: 0
                    loops: Animation.Infinite
                    running: root.speechRecognizer && root.speechRecognizer.processing
                    target: statusLed
                    to: 360
                }
            }

            // Status text
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                font.pixelSize: 10
                text: {
                    if (!root.speechRecognizer)
                        return "No Recognizer";
                    if (!root.speechRecognizer.enabled)
                        return "Disabled";
                    if (root.speechRecognizer.errorString !== "")
                        return "Error";
                    if (root.speechRecognizer.processing)
                        return "Processing...";
                    if (root.speechRecognizer.listening)
                        return "Listening...";
                    return "Initializing...";
                }
            }

            // Kerfur detection indicator
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                color: "#ff44ff"
                height: 18
                radius: 9
                visible: root.speechRecognizer && root.speechRecognizer.kerfurDetected
                width: 65

                SequentialAnimation on scale {
                    loops: 3
                    running: parent.visible

                    NumberAnimation {
                        duration: 150
                        to: 1.3
                    }

                    NumberAnimation {
                        duration: 150
                        to: 1.0
                    }
                }

                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font.bold: true
                    font.pixelSize: 10
                    text: "KERFUR!"
                }

                // Glow effect
                Rectangle {
                    anchors.centerIn: parent
                    border.color: "#ff44ff"
                    border.width: 2
                    color: "transparent"
                    height: parent.height + 4
                    opacity: 0.5
                    radius: 11
                    width: parent.width + 4

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: parent.parent.visible

                        NumberAnimation {
                            duration: 500
                            to: 0.2
                        }

                        NumberAnimation {
                            duration: 500
                            to: 0.8
                        }
                    }
                }
            }
        }

        // Separator line
        Rectangle {
            color: "#555"
            height: 1
            visible: root.speechRecognizer && (root.speechRecognizer.lastText !== "" || root.speechRecognizer.processing)
            width: parent.width
        }

        // Last recognized text
        Rectangle {
            border.color: "#555"
            border.width: 1
            color: "#3a3a3a"
            height: textContent.height + 12
            radius: 4
            visible: root.speechRecognizer && root.speechRecognizer.lastText !== ""
            width: parent.width

            Text {
                id: textContent

                anchors.left: parent.left
                anchors.margins: 6
                anchors.right: parent.right
                anchors.top: parent.top
                color: "white"
                font.pixelSize: 11
                text: root.speechRecognizer ? root.speechRecognizer.lastText : ""
                wrapMode: Text.WordWrap

                // Subtle animation when text changes
                Behavior on text {
                    SequentialAnimation {
                        NumberAnimation {
                            duration: 100
                            property: "opacity"
                            target: textContent
                            to: 0.7
                        }

                        NumberAnimation {
                            duration: 200
                            property: "opacity"
                            target: textContent
                            to: 1.0
                        }
                    }
                }
            }
        }

        // Confidence indicator
        Row {
            spacing: 8
            visible: root.speechRecognizer && root.speechRecognizer.confidence > 0

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#aaa"
                font.pixelSize: 9
                text: "Confidence:"
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: "#444"
                border.width: 1
                color: "#1a1a1a"
                height: 8
                radius: 4
                width: 60

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: {
                        if (!root.speechRecognizer)
                            return "gray";
                        const conf = root.speechRecognizer.confidence;
                        if (conf > 0.8)
                            return "lime";
                        if (conf > 0.6)
                            return "yellow";
                        if (conf > 0.4)
                            return "orange";
                        return "red";
                    }
                    height: parent.height - 2
                    radius: 3
                    width: parent.width * (root.speechRecognizer ? root.speechRecognizer.confidence : 0)
                    x: 1

                    Behavior on width {
                        NumberAnimation {
                            duration: 300
                        }
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.speechRecognizer)
                        return "gray";
                    const conf = root.speechRecognizer.confidence;
                    if (conf > 0.7)
                        return "lime";
                    if (conf > 0.4)
                        return "yellow";
                    return "orange";
                }
                font.pixelSize: 9
                text: root.speechRecognizer ? (root.speechRecognizer.confidence * 100).toFixed(0) + "%" : "0%"
            }
        }

        // Error message
        Rectangle {
            border.color: "#aa4444"
            border.width: 1
            color: "#4a2a2a"
            height: errorText.height + 8
            radius: 4
            visible: root.speechRecognizer && root.speechRecognizer.errorString !== ""
            width: parent.width

            Text {
                id: errorText

                anchors.left: parent.left
                anchors.margins: 4
                anchors.right: parent.right
                anchors.top: parent.top
                color: "#ffaaaa"
                font.pixelSize: 9
                text: root.speechRecognizer ? ("⚠ " + root.speechRecognizer.errorString) : ""
                wrapMode: Text.WordWrap
            }
        }
    }
}
