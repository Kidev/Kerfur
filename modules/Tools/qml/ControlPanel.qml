import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Vision
import Hearing
import qml 1.0

Rectangle {
    id: root

    required property FaceTracker faceTracker

    // Passed in rather than declared here. Two SpeechRecognizer instances are
    // two microphones, two whisper contexts and two transcription loops
    // competing for the same words, which is what the second one did.
    required property SpeechRecognizer speechRecognizer

    color: "#333333"
    opacity: 0.7
    visible: KSettings.showControls
    z: 100

    Timer {
        id: colorAnimationTimer

        property real hue: 0

        interval: 16 // approximately 60 FPS
        repeat: true
        running: false

        onTriggered: {
            hue = (hue + 0.005) % 1.0;
            KSettings.glowColor = Qt.hsva(hue, 1.0, 1.0, 1.0);
        }
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentHeight: controlsColumn.height
        contentWidth: parent.width

        Column {
            spacing: 1

            Text {
                color: "white"
                font.bold: true
                font.pointSize: 12
                text: "kerfur"
            }

            Text {
                color: "white"
                font.italic: true
                font.pointSize: 8
                text: "kidev"
                topPadding: -5
            }
        }

        Column {
            id: controlsColumn

            padding: 10
            spacing: 10
            topPadding: 50
            width: parent.width

            // Camera Section
            Text {
                color: "white"
                font.bold: true
                text: "Camera Settings"
            }

            // Face Tracking Enable
            Row {
                spacing: 5

                Rectangle {
                    border.color: KSettings.faceTrackingEnabled ? "green" : "gray"
                    color: KSettings.faceTrackingEnabled ? "lightgreen" : "darkgray"
                    height: 30
                    width: 60

                    Text {
                        anchors.centerIn: parent
                        color: "black"
                        font.bold: true
                        text: KSettings.faceTrackingEnabled ? "ON" : "OFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: KSettings.faceTrackingEnabled = !KSettings.faceTrackingEnabled
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    text: "Face Tracking"
                }
            }

            // Camera Feed Display
            Text {
                color: "white"
                text: "Camera Feed (Click to Enlarge)"
            }

            CameraFeedDisplay {
                faceTracker: root.faceTracker
                height: (parent.width - 20) * 0.75  // 4:3 aspect ratio
                width: parent.width - 20
            }

            // Eye Movement Range
            Text {
                color: "white"
                text: "Eye Movement Range: " + eyeMovementRangeSlider.value.toFixed(1)
            }

            KSlider {
                id: eyeMovementRangeSlider

                from: 0.1
                settingName: "eyeMovementRange"
                to: 3.0
                width: parent.width - 20
            }

            // Detection Confidence
            Text {
                color: "white"
                text: "Detection Confidence: " + detectionConfidenceSlider.value.toFixed(2)
            }

            Slider {
                id: detectionConfidenceSlider

                from: 0.3
                to: 0.9
                value: root.faceTracker ? root.faceTracker.detectionConfidence : 0.7
                width: parent.width - 20

                onValueChanged: {
                    if (root.faceTracker) {
                        root.faceTracker.detectionConfidence = value;
                    }
                }
            }

            // Tracking Smoothing
            Text {
                color: "white"
                text: "Tracking Smoothing: " + trackingSmoothingSlider.value.toFixed(2)
            }

            Slider {
                id: trackingSmoothingSlider

                from: 0.0
                to: 1.0
                value: root.faceTracker ? root.faceTracker.trackingSmoothing : 0.3
                width: parent.width - 20

                onValueChanged: {
                    if (root.faceTracker) {
                        root.faceTracker.trackingSmoothing = value;
                    }
                }
            }

            Text {
                color: "white"
                font.bold: true
                text: "Speech Recognition"
                topPadding: 15
            }

            // Speech Recognition Enable
            Row {
                spacing: 5

                Rectangle {
                    border.color: KSettings.speechRecognitionEnabled ? "green" : "gray"
                    color: KSettings.speechRecognitionEnabled ? "lightgreen" : "darkgray"
                    height: 30
                    width: 60

                    Text {
                        anchors.centerIn: parent
                        color: "black"
                        font.bold: true
                        text: KSettings.speechRecognitionEnabled ? "ON" : "OFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: KSettings.speechRecognitionEnabled = !KSettings.speechRecognitionEnabled
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    text: "Listen for 'Kerfur'"
                }
            }

            // Audio input device selector
            Row {
                spacing: 8
                width: parent.width

                Text {
                    color: "white"
                    text: "Audio Device:"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Button {
                    text: "Refresh"
                    height: 25
                    anchors.verticalCenter: parent.verticalCenter

                    background: Rectangle {
                        color: parent.hovered ? "#666666" : "#555555"
                        border.color: "#777777"
                        border.width: 1
                        radius: 3
                    }
                    contentItem: Text {
                        color: "white"
                        text: parent.text
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: root.speechRecognizer.refreshDevices()
                }
            }

            ComboBox {
                id: audioDeviceCombo

                width: parent.width - 20
                model: root.speechRecognizer.availableDevices
                currentIndex: {
                    if (root.speechRecognizer && root.speechRecognizer.availableDevices) {
                        return root.speechRecognizer.availableDevices.indexOf(root.speechRecognizer.currentDevice);
                    }
                    return -1;
                }

                background: Rectangle {
                    border.color: "#777777"
                    border.width: 1
                    color: "#555555"
                    radius: 4
                }
                contentItem: Text {
                    color: "white"
                    leftPadding: 8
                    text: audioDeviceCombo.displayText || "No device selected"
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 10
                }
                delegate: ItemDelegate {
                    id: deviceDelegate
                    required property string modelData
                    required property int index

                    highlighted: audioDeviceCombo.highlightedIndex === deviceDelegate.index
                    width: audioDeviceCombo.width

                    background: Rectangle {
                        color: deviceDelegate.highlighted ? "#666666" : "transparent"
                    }
                    contentItem: Text {
                        color: "white"
                        text: deviceDelegate.modelData
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 10
                    }
                }
                popup: Popup {
                    implicitHeight: contentItem.implicitHeight
                    padding: 1
                    width: audioDeviceCombo.width
                    y: audioDeviceCombo.height - 1

                    background: Rectangle {
                        border.color: "#777777"
                        color: "#555555"
                        radius: 4
                    }
                    contentItem: ListView {
                        clip: true
                        currentIndex: audioDeviceCombo.highlightedIndex
                        implicitHeight: contentHeight
                        model: audioDeviceCombo.popup.visible ? audioDeviceCombo.delegateModel : null

                        ScrollIndicator.vertical: ScrollIndicator {
                        }
                    }
                }

                onCurrentTextChanged: {
                    if (root.speechRecognizer && currentText !== root.speechRecognizer.currentDevice && currentText !== "") {
                        root.speechRecognizer.setDevice(currentText);
                    }
                }
            }

            // Speech display
            Text {
                color: "white"
                text: "Speech Monitor"
            }

            SpeechDisplay {
                speechRecognizer: root.speechRecognizer
                width: parent.width - 20
                height: 120
            }

            // Visual Effects Section
            Text {
                color: "white"
                font.bold: true
                text: "Visual Effects"
                topPadding: 15
            }

            // LED Size slider
            Text {
                color: "white"
                text: "LED Size: " + ledSizeSlider.value.toFixed(1)
            }

            KSlider {
                id: ledSizeSlider

                from: 1
                settingName: "ledScreenLedSize"
                to: 50
                width: parent.width - 20
            }

            // Blur Multiplier slider
            Text {
                color: "white"
                text: "Blur Mult: " + blurMultiplierSlider.value.toFixed(2)
            }

            KSlider {
                id: blurMultiplierSlider

                from: 0
                settingName: "blurMultiplier"
                to: 5
                width: parent.width - 20
            }

            // Up down angle
            Text {
                color: "white"
                text: "Up/down angle: " + upDownAngleSlider.value.toFixed(0)
            }

            KSlider {
                id: upDownAngleSlider

                from: 0
                settingName: "upDownAngle"
                to: 180
                width: parent.width - 20
            }

            // Left right angle
            Text {
                color: "white"
                text: "Left/right angle: " + leftRightAngleSlider.value.toFixed(0)
            }

            KSlider {
                id: leftRightAngleSlider

                from: 0
                settingName: "leftRightAngle"
                to: 180
                width: parent.width - 20
            }

            // Spread min
            Text {
                color: "white"
                text: "Spread min: " + spreadMinSlider.value.toFixed(2)
            }

            KSlider {
                id: spreadMinSlider

                from: 0.0
                settingName: "maskSpreadAtMin"
                to: 1.0
                width: parent.width - 20
            }

            // Spread max
            Text {
                color: "white"
                text: "Spread max: " + spreadMaxSlider.value.toFixed(2)
            }

            KSlider {
                id: spreadMaxSlider

                from: 0.0
                settingName: "maskSpreadAtMax"
                to: 1.0
                width: parent.width - 20
            }

            // Threshold min
            Text {
                color: "white"
                text: "Threshold min: " + thresholdMinSlider.value.toFixed(2)
            }

            KSlider {
                id: thresholdMinSlider

                from: 0.0
                settingName: "maskThresholdMin"
                to: 1.0
                width: parent.width - 20
            }

            // Threshold max
            Text {
                color: "white"
                text: "Threshold max: " + thresholdMaxSlider.value.toFixed(2)
            }

            KSlider {
                id: thresholdMaxSlider

                from: 0.0
                settingName: "maskThresholdMax"
                to: 1.0
                width: parent.width - 20
            }

            // Glow Blend Mode
            Text {
                color: "white"
                text: "Blend Mode: " + ["Additive", "Screen", "Replace", "Outer"][glowBlendModeSlider.value]
            }

            KSlider {
                id: glowBlendModeSlider

                from: 0
                settingName: "glowBlendMode"
                stepSize: 1
                to: 3
                width: parent.width - 20
            }

            // Glow Bloom slider
            Text {
                color: "white"
                text: "Glow Bloom: " + glowBloomSlider.value.toFixed(2)
            }

            KSlider {
                id: glowBloomSlider

                from: 0
                settingName: "glowBloom"
                to: 2
                width: parent.width - 20
            }

            // Glow Blur Amount slider
            Text {
                color: "white"
                text: "Glow Blur: " + glowBlurAmountSlider.value.toFixed(3)
            }

            KSlider {
                id: glowBlurAmountSlider

                from: 0
                settingName: "glowBlurAmount"
                to: 1
                width: parent.width - 20
            }

            // Glow Max Brightness slider
            Text {
                color: "white"
                text: "Glow Max: " + glowMaxBrightnessSlider.value.toFixed(2)
            }

            KSlider {
                id: glowMaxBrightnessSlider

                from: 0
                settingName: "glowMaxBrightness"
                to: 2
                width: parent.width - 20
            }

            // Glow Color
            Text {
                color: "white"
                text: "Glow Color"
            }

            Row {
                spacing: 5

                Rectangle {
                    border.color: "gray"
                    color: "cyan"
                    height: 30
                    width: 30

                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(0, 1, 1, 1);
                        }
                    }
                }

                Rectangle {
                    border.color: "gray"
                    color: "green"
                    height: 30
                    width: 30

                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(0, 1, 0, 1);
                        }
                    }
                }

                Rectangle {
                    border.color: "gray"
                    color: "magenta"
                    height: 30
                    width: 30

                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            colorAnimationTimer.running = false;
                            KSettings.glowColor = Qt.rgba(1, 0, 1, 1);
                        }
                    }
                }

                Rectangle {
                    id: gamerColorRect

                    border.color: "gray"
                    height: 30
                    width: 30

                    gradient: Gradient {
                        GradientStop {
                            color: "red"
                            position: 0.0
                        }

                        GradientStop {
                            color: "yellow"
                            position: 0.2
                        }

                        GradientStop {
                            color: "green"
                            position: 0.4
                        }

                        GradientStop {
                            color: "cyan"
                            position: 0.6
                        }

                        GradientStop {
                            color: "blue"
                            position: 0.8
                        }

                        GradientStop {
                            color: "magenta"
                            position: 1.0
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        color: "white"
                        font.bold: true
                        font.pixelSize: 8
                        text: "GAMER"
                    }

                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            colorAnimationTimer.running = true;
                        }
                    }
                }
            }
        }
    }
}
