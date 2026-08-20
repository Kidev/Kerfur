// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick

/// What the microphone is doing, in the order the answers are wanted: whether
/// it is running at all, whether the room is loud enough, who is talking, and
/// only then the words.
///
/// It sizes itself from its contents. The panel that shows it cannot know how
/// many utterances are on screen, and a fixed height either clipped the newest
/// one or left a hole under the oldest.
///
/// The colours are the control panel's, repeated here rather than imported:
/// Hearing cannot import Tools, because Tools imports Hearing.
Rectangle {
    id: root

    required property var speechRecognizer

    readonly property color colorAccent: "#8ad8ff"
    readonly property color colorDim: "#98a0ac"
    readonly property color colorLine: "#3c414a"
    readonly property color colorOk: "#7cff5a"
    readonly property color colorText: "#eceff4"

    function levelColor(level) {
        if (!root.speechRecognizer || !root.speechRecognizer.listening)
            return "#6b7280";
        if (level > 0.8)
            return "#ff6b6b";
        if (level > 0.6)
            return "#ffb74d";
        if (level > 0.3)
            return root.colorOk;
        if (level > 0.1)
            return "#4f9a4f";
        return "#6b7280";
    }

    function statusColor() {
        if (!root.speechRecognizer || !root.speechRecognizer.enabled)
            return "#4a4f57";
        if (root.speechRecognizer.errorString !== "")
            return "#ff6b6b";
        if (root.speechRecognizer.processing)
            return "#ffb74d";
        if (root.speechRecognizer.listening)
            return root.colorOk;
        return "#ffb74d";
    }

    function statusText() {
        if (!root.speechRecognizer)
            return "no recognizer";
        if (!root.speechRecognizer.enabled)
            return "disabled";
        if (root.speechRecognizer.errorString !== "")
            return "error";
        if (root.speechRecognizer.processing)
            return "transcribing";
        if (root.speechRecognizer.listening)
            return "listening";
        return "starting";
    }

    border.color: root.colorLine
    border.width: 1
    color: "#1f2226"
    implicitHeight: column.implicitHeight + 16
    radius: 4

    Column {
        id: column

        anchors.left: parent.left
        anchors.margins: 8
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 7

        // Running, and doing what. The lamp is the same three colours the panel
        // header uses, so a glance at either says the same thing.
        Row {
            spacing: 6
            width: parent.width

            Rectangle {
                id: statusLed

                anchors.verticalCenter: parent.verticalCenter
                color: root.statusColor()
                height: 10
                radius: 5
                width: 10

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: root.speechRecognizer && root.speechRecognizer.listening && !root.speechRecognizer.processing

                    NumberAnimation {
                        duration: 900
                        to: 0.35
                    }

                    NumberAnimation {
                        duration: 900
                        to: 1.0
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: root.colorText
                font.pixelSize: 10
                text: root.statusText()
            }

            // The wake word, when it lands. It is the one event in this panel
            // worth an animation: everything else here is a level or a state,
            // and this is a thing that happened.
            Rectangle {
                id: wakeBadge

                anchors.verticalCenter: parent.verticalCenter
                color: "#ff44ff"
                height: 16
                radius: 8
                visible: root.speechRecognizer && root.speechRecognizer.kerfurDetected
                width: 58

                SequentialAnimation on scale {
                    loops: 3
                    running: wakeBadge.visible

                    NumberAnimation {
                        duration: 150
                        to: 1.25
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
                    font.pixelSize: 9
                    text: "KERFUR"
                }
            }
        }

        // The level, and beside it the gate. The meter says how loud the room
        // is; the lamp says whether the segmenter thinks any of it is somebody
        // talking, which is what actually decides what gets transcribed.
        Row {
            spacing: 6
            width: parent.width

            Rectangle {
                id: meter

                anchors.verticalCenter: parent.verticalCenter
                border.color: root.colorLine
                border.width: 1
                color: "#15171a"
                height: 12
                radius: 6
                width: parent.width - 62

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.levelColor(root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0)
                    height: parent.height - 2
                    radius: 5
                    width: Math.max(0, (parent.width - 2) * (root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0))
                    x: 1

                    Behavior on width {
                        NumberAnimation {
                            duration: 50
                        }
                    }
                }

                // The recent maximum, decaying. Without it a level that peaks
                // between two repaints is a level that was never there.
                Rectangle {
                    id: peakIndicator

                    property real peakLevel: 0

                    color: "#ffe082"
                    height: parent.height - 4
                    opacity: 0.9
                    visible: peakIndicator.peakLevel > 0.1
                    width: 2
                    x: Math.min(parent.width - 3, 1 + ((parent.width - 2) * peakIndicator.peakLevel))
                    y: 2

                    Timer {
                        interval: 100
                        repeat: true
                        running: root.speechRecognizer && root.speechRecognizer.listening

                        onTriggered: {
                            const level = root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0;
                            peakIndicator.peakLevel = level > peakIndicator.peakLevel ? level : Math.max(0, peakIndicator.peakLevel - 0.02);
                        }
                    }
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: root.colorLine
                border.width: 1
                color: root.speechRecognizer && root.speechRecognizer.speechDetected ? root.colorOk : "#33373e"
                height: 10
                radius: 5
                width: 10
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: root.levelColor(root.speechRecognizer ? root.speechRecognizer.volumeLevel : 0)
                font.pixelSize: 9
                horizontalAlignment: Text.AlignRight
                text: root.speechRecognizer ? (root.speechRecognizer.volumeLevel * 100).toFixed(0) + "%" : "0%"
                width: 26
            }
        }

        // Who is speaking, and whether that voice may give orders. Everybody is
        // transcribed; the second half of this row is the one bit that decides
        // whether what they said acts.
        Row {
            spacing: 6
            visible: root.speechRecognizer && root.speechRecognizer.voiceIdentityReady
            width: parent.width

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: root.colorDim
                font.pixelSize: 9
                text: "voice"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.speechRecognizer || root.speechRecognizer.speaker === "")
                        return root.colorDim;
                    return root.speechRecognizer.speaker === "kidev" ? "#ff44ff" : root.colorOk;
                }
                font.bold: root.speechRecognizer && root.speechRecognizer.speaker !== ""
                font.pixelSize: 10
                text: {
                    if (!root.speechRecognizer)
                        return "";
                    if (root.speechRecognizer.speaker === "")
                        return "unknown";
                    return root.speechRecognizer.speaker + " " + Math.round(root.speechRecognizer.speakerConfidence * 100) + "%";
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: root.speechRecognizer && root.speechRecognizer.commandAuthority ? "#3f5c31" : root.colorLine
                border.width: 1
                color: root.speechRecognizer && root.speechRecognizer.commandAuthority ? "#2c4632" : "#2a2d33"
                height: authorityText.height + 4
                radius: 3
                width: authorityText.width + 10

                Text {
                    id: authorityText

                    anchors.centerIn: parent
                    color: root.speechRecognizer && root.speechRecognizer.commandAuthority ? root.colorOk : root.colorDim
                    font.pixelSize: 9
                    text: {
                        if (!root.speechRecognizer)
                            return "";
                        if (root.speechRecognizer.speakerPriority >= 1000000)
                            return "unranked, heard only";
                        return "rank " + root.speechRecognizer.speakerPriority + (root.speechRecognizer.commandAuthority ? ", may command" : ", heard only");
                    }
                }
            }
        }

        Rectangle {
            color: root.colorLine
            height: 1
            visible: root.speechRecognizer && root.speechRecognizer.transcripts.length > 0
            width: parent.width
        }

        // The last few utterances, newest first. A list rather than one line,
        // because one line cannot show that the same sentence was heard twice
        // and that is precisely what used to look like a frozen display. The
        // newest carries a lit edge, so which one just arrived is visible
        // without having to remember what was there a moment ago.
        Column {
            spacing: 3
            width: parent.width

            Repeater {
                model: root.speechRecognizer ? root.speechRecognizer.transcripts : []

                delegate: Rectangle {
                    id: entry

                    required property int index
                    required property string modelData

                    readonly property bool newest: entry.index === 0

                    color: entry.newest ? "#2b3036" : "#24272c"
                    height: line.implicitHeight + 10
                    radius: 3
                    width: parent.width

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.top: parent.top
                        color: entry.newest ? root.colorAccent : "#3c414a"
                        radius: 1.5
                        width: 2
                    }

                    Text {
                        id: line

                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: entry.newest ? root.colorText : root.colorDim
                        font.pixelSize: entry.newest ? 11 : 10
                        text: entry.modelData
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Row {
            spacing: 6
            visible: root.speechRecognizer && root.speechRecognizer.confidence > 0
            width: parent.width

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: root.colorDim
                font.pixelSize: 9
                text: "confidence"
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: root.colorLine
                border.width: 1
                color: "#15171a"
                height: 6
                radius: 3
                width: parent.width - 100

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: {
                        if (!root.speechRecognizer)
                            return root.colorDim;
                        const value = root.speechRecognizer.confidence;
                        if (value > 0.7)
                            return root.colorOk;
                        if (value > 0.4)
                            return "#ffb74d";
                        return "#ff6b6b";
                    }
                    height: parent.height - 2
                    radius: 2
                    width: Math.max(0, (parent.width - 2) * (root.speechRecognizer ? root.speechRecognizer.confidence : 0))
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
                color: root.colorDim
                font.pixelSize: 9
                text: root.speechRecognizer ? (root.speechRecognizer.confidence * 100).toFixed(0) + "%" : "0%"
            }
        }

        Rectangle {
            border.color: "#8a3a3a"
            border.width: 1
            color: "#3a2326"
            height: errorText.implicitHeight + 10
            radius: 3
            visible: root.speechRecognizer && root.speechRecognizer.errorString !== ""
            width: parent.width

            Text {
                id: errorText

                anchors.left: parent.left
                anchors.margins: 5
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                color: "#ffb4b4"
                font.pixelSize: 9
                text: root.speechRecognizer ? root.speechRecognizer.errorString : ""
                wrapMode: Text.WordWrap
            }
        }
    }
}
