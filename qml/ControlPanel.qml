import QtQuick
import QtQuick.Window
import QtQuick.Controls

Rectangle {
    id: root

    required property KSettings settings

    color: "#333333"
    opacity: 0.7
    z: 100

    Timer {
        id: colorAnimationTimer

        property real hue: 0

        interval: 16 // approximately 60 FPS
        repeat: true
        running: false

        onTriggered: {
            hue = (hue + 0.005) % 1.0;
            root.settings.glowColor = Qt.hsva(hue, 1.0, 1.0, 1.0);
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

            // LED Size slider
            Text {
                color: "white"
                text: "LED Size: " + ledSizeSlider.value.toFixed(1)
            }

            KSlider {
                id: ledSizeSlider

                from: 1
                settingName: "ledScreenLedSize"
                settingsObject: root.settings
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

                decimals: 2
                from: 0
                settingName: "blurMultiplier"
                settingsObject: root.settings
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

                decimals: 0
                from: 0
                settingName: "upDownAngle"
                settingsObject: root.settings
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

                decimals: 0
                from: 0
                settingName: "leftRightAngle"
                settingsObject: root.settings
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

                decimals: 2
                from: 0.0
                settingName: "maskSpreadAtMin"
                settingsObject: root.settings
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

                decimals: 2
                from: 0.0
                settingName: "maskSpreadAtMax"
                settingsObject: root.settings
                to: 1.0
                width: parent.width - 20
            }

            // Threshold min
            Text {
                color: "white"
                text: "threshold min: " + thresholdMinSlider.value.toFixed(2)
            }

            KSlider {
                id: thresholdMinSlider

                decimals: 2
                from: 0.0
                settingName: "maskThresholdMin"
                settingsObject: root.settings
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

                decimals: 2
                from: 0.0
                settingName: "maskThresholdMax"
                settingsObject: root.settings
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

                decimals: 0
                from: 0
                settingName: "glowBlendMode"
                settingsObject: root.settings
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

                decimals: 2
                from: 0
                settingName: "glowBloom"
                settingsObject: root.settings
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

                decimals: 3
                from: 0
                settingName: "glowBlurAmount"
                settingsObject: root.settings
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

                decimals: 2
                from: 0
                settingName: "glowMaxBrightness"
                settingsObject: root.settings
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
                            root.settings.glowColor = Qt.rgba(0, 1, 1, 1);
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
                            root.settings.glowColor = Qt.rgba(0, 1, 0, 1);
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
                            root.settings.glowColor = Qt.rgba(1, 0, 1, 1);
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
