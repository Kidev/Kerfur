import QtQuick

Rectangle {
    id: root

    anchors.right: parent.right
    anchors.top: parent.top
    color: "#333333"
    height: parent.height
    opacity: 0.7
    visible: root.showControls
    width: 150
    z: 100

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

            Slider {
                id: ledSizeSlider
                from: 1
                to: 50
                value: settings.ledScreenLedSize
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "ledScreenLedSize"
                value: ledSizeSlider.value
            }

            // Blur Multiplier slider
            Text {
                color: "white"
                text: "Blur Mult: " + blurMultiplierSlider.value.toFixed(2)
            }

            Slider {
                id: blurMultiplierSlider
                from: 0
                to: 5
                value: settings.blurMultiplier
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "blurMultiplier"
                value: blurMultiplierSlider.value
            }

            // Up down angle
            Text {
                color: "white"
                text: "Up/down angle: " + upDownAngleSlider.value.toFixed(0)
            }

            Slider {
                id: upDownAngleSlider
                from: 0
                to: 180
                value: root.upDownAngle
                width: parent.width - 20
            }

            Binding {
                target: root
                property: "upDownAngle"
                value: upDownAngleSlider.value
            }

            // Left right angle
            Text {
                color: "white"
                text: "Left/right angle: " + leftRightAngleSlider.value.toFixed(0)
            }

            Slider {
                id: leftRightAngleSlider
                from: 0
                to: 180
                value: root.leftRightAngle
                width: parent.width - 20
            }

            Binding {
                target: root
                property: "leftRightAngle"
                value: leftRightAngleSlider.value
            }

            // Spread min
            Text {
                color: "white"
                text: "Spread min: " + spreadMinSlider.value.toFixed(2)
            }

            Slider {
                id: spreadMinSlider
                from: 0.0
                to: 1.0
                value: settings.maskSpreadAtMin
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "maskSpreadAtMin"
                value: spreadMinSlider.value
            }

            // Spread max
            Text {
                color: "white"
                text: "Spread max: " + spreadMaxSlider.value.toFixed(2)
            }

            Slider {
                id: spreadMaxSlider
                from: 0.0
                to: 1.0
                value: settings.maskSpreadAtMax
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "maskSpreadAtMax"
                value: spreadMaxSlider.value
            }

            // Threshold min
            Text {
                color: "white"
                text: "threshold min: " + thresholdMinSlider.value.toFixed(2)
            }

            Slider {
                id: thresholdMinSlider
                from: 0.0
                to: 1.0
                value: settings.maskThresholdMin
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "maskThresholdMin"
                value: thresholdMinSlider.value
            }

            // Threshold max
            Text {
                color: "white"
                text: "Threshold max: " + thresholdMaxSlider.value.toFixed(2)
            }

            Slider {
                id: thresholdMaxSlider
                from: 0.0
                to: 1.0
                value: settings.maskThresholdMax
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "maskThresholdMax"
                value: thresholdMaxSlider.value
            }

            // Glow Blend Mode
            Text {
                color: "white"
                text: "Blend Mode: " + ["Additive", "Screen", "Replace", "Outer"][glowBlendModeSlider.value]
            }

            Slider {
                id: glowBlendModeSlider
                from: 0
                stepSize: 1
                to: 3
                value: settings.glowBlendMode
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "glowBlendMode"
                value: glowBlendModeSlider.value
            }

            // Glow Bloom slider
            Text {
                color: "white"
                text: "Glow Bloom: " + glowBloomSlider.value.toFixed(2)
            }

            Slider {
                id: glowBloomSlider
                from: 0
                to: 2
                value: settings.glowBloom
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "glowBloom"
                value: glowBloomSlider.value
            }

            // Glow Blur Amount slider
            Text {
                color: "white"
                text: "Glow Blur: " + glowBlurAmountSlider.value.toFixed(3)
            }

            Slider {
                id: glowBlurAmountSlider
                from: 0
                to: 1
                value: settings.glowBlurAmount
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "glowBlurAmount"
                value: glowBlurAmountSlider.value
            }

            // Glow Max Brightness slider
            Text {
                color: "white"
                text: "Glow Max: " + glowMaxBrightnessSlider.value.toFixed(2)
            }

            Slider {
                id: glowMaxBrightnessSlider
                from: 0
                to: 2
                value: settings.glowMaxBrightness
                width: parent.width - 20
            }

            Binding {
                target: settings
                property: "glowMaxBrightness"
                value: glowMaxBrightnessSlider.value
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
                            settings.glowColor = Qt.rgba(0, 1, 1, 1);
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
                            settings.glowColor = Qt.rgba(0, 1, 0, 1);
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
                            settings.glowColor = Qt.rgba(1, 0, 1, 1);
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
