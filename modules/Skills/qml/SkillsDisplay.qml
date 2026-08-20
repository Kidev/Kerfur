pragma ComponentBehavior: Bound

import QtQuick
import Skills

// What the skills are doing, for the control panel. Read only: everything here
// is a consequence of what was said, and the two buttons only answer a question
// Kerfur already asked out loud.
Rectangle {
    id: root

    border.color: "#444"
    border.width: 1
    color: "#2a2a2a"
    implicitHeight: column.implicitHeight + 16
    radius: 4

    Column {
        id: column

        anchors.left: parent.left
        anchors.margins: 8
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        Row {
            spacing: 6
            width: parent.width

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Skills.skillNames.length > 0 ? "#7cff5a" : "#888"
                font.bold: true
                font.pixelSize: 10
                text: "Skills"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#aaa"
                font.pixelSize: 9
                text: Skills.skillNames.length > 0 ? Skills.skillNames.join(", ") : "none loaded"
            }
        }

        Text {
            color: "#666"
            font.pixelSize: 8
            text: "via " + Skills.backendName + (Skills.directory !== "" ? " - " + Skills.directory : "")
            width: parent.width
            wrapMode: Text.WrapAnywhere
        }

        Rectangle {
            border.color: "#aa4444"
            border.width: 1
            color: "#4a2a2a"
            height: problemText.height + 8
            radius: 3
            visible: Skills.problem !== ""
            width: parent.width

            Text {
                id: problemText

                anchors.left: parent.left
                anchors.margins: 4
                anchors.right: parent.right
                anchors.top: parent.top
                color: "#ffaaaa"
                font.pixelSize: 8
                text: Skills.problem
                wrapMode: Text.WordWrap
            }
        }

        // The last thing heard, and what it was taken to mean. The refusal case
        // is the one worth seeing: a command that was understood and declined
        // looks exactly like one that was never heard, unless it says so.
        Column {
            spacing: 2
            visible: Skills.lastUtterance !== ""
            width: parent.width

            Text {
                color: "white"
                font.pixelSize: 10
                text: "\"" + Skills.lastUtterance + "\""
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Text {
                color: {
                    if (Skills.lastIntent === "")
                        return "#888";
                    return Skills.lastAuthorised ? "#7cff5a" : "#ffaa44";
                }
                font.pixelSize: 9
                text: {
                    const who = Skills.lastSpeaker === "" ? "a stranger" : Skills.lastSpeaker;
                    if (Skills.lastIntent === "")
                        return who + " - not understood";
                    return who + " - " + Skills.lastIntent;
                }
                width: parent.width
                wrapMode: Text.WordWrap
            }
        }

        // An outstanding question. It is answered by voice; these are here so
        // that a microphone that cannot hear the answer is not a dead end.
        Rectangle {
            border.color: "#ffaa44"
            border.width: 1
            color: "#3a3222"
            height: confirmColumn.implicitHeight + 8
            radius: 3
            visible: Skills.awaitingConfirmation
            width: parent.width

            Column {
                id: confirmColumn

                anchors.left: parent.left
                anchors.margins: 4
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 4

                Text {
                    color: "#ffd9a0"
                    font.pixelSize: 9
                    text: Skills.question
                    width: parent.width
                    wrapMode: Text.WordWrap
                }

                Row {
                    spacing: 6

                    Repeater {
                        model: [
                            {
                                "label": "Yes",
                                "accept": true
                            },
                            {
                                "label": "No",
                                "accept": false
                            }
                        ]

                        delegate: Rectangle {
                            id: answer

                            required property var modelData

                            border.color: "#666"
                            border.width: 1
                            color: mouse.pressed ? "#555" : "#3a3a3a"
                            height: 18
                            radius: 3
                            width: 44

                            Text {
                                anchors.centerIn: parent
                                color: "white"
                                font.pixelSize: 9
                                text: answer.modelData.label
                            }

                            MouseArea {
                                id: mouse

                                anchors.fill: parent

                                onClicked: answer.modelData.accept ? Skills.confirmPending() : Skills.cancelPending()
                            }
                        }
                    }
                }
            }
        }

        Text {
            color: "#c8b0ff"
            font.pixelSize: 9
            text: "says: " + Skills.lastSpoken
            visible: Skills.lastSpoken !== ""
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Rectangle {
            color: "#555"
            height: 1
            visible: Blackboard.keys.length > 0
            width: parent.width
        }

        // The variables, live. This is the whole state of every skill, and
        // seeing it change while talking is how a skill gets debugged.
        Column {
            spacing: 1
            width: parent.width

            Repeater {
                model: Blackboard.keys

                delegate: Row {
                    id: variable

                    required property string modelData

                    spacing: 4

                    Text {
                        color: "#9e9e9e"
                        font.pixelSize: 9
                        text: variable.modelData + ":"
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 9
                        // Through the snapshot rather than through `values`,
                        // because the key is only known at run time and a
                        // binding cannot depend on a property it cannot name.
                        text: String(Blackboard.snapshot[variable.modelData] ?? "")
                    }
                }
            }
        }

        Rectangle {
            border.color: "#666"
            border.width: 1
            color: reloadMouse.pressed ? "#555" : "#3a3a3a"
            height: 18
            radius: 3
            width: 60

            Text {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 9
                text: "Reload"
            }

            MouseArea {
                id: reloadMouse

                anchors.fill: parent

                onClicked: Skills.reload()
            }
        }
    }
}
