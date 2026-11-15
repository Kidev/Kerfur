pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Skills

// What the skills are doing, for the control panel, and where a skill is added
// or edited. Everything else here is a consequence of what was said, and the
// Yes and No buttons only answer a question Kerfur already asked out loud.
Rectangle {
    id: root

    // Where a person writing a skill reads what one can do.
    readonly property string referenceUrl: "https://github.com/Kidev/Kerfur#skills"

    // What a new skill starts as: the smallest complete one, so the editor
    // opens on something that already loads.
    readonly property string skillTemplate: "import Skills\n\nSkill {\n    id: skill\n\n    name: \"greeting\"\n    description: \"Says hello back\"\n\n    Trigger {\n        intent: \"when somebody says hello\"\n        phrases: [\"hello kerfur\", \"hi kerfur\"]\n        // Anybody may say hello, not only rank 0.\n        authority: Trigger.Anyone\n\n        onFired: skill.say(\"Hello!\")\n    }\n}\n"

    function editSkill(entry) {
        editor.replacing = entry.added ? entry.file : "";
        editor.title = "Edit " + entry.file;
        editor.source = Skills.sourceOf(entry.path);
        editor.open();
    }

    function newSkill() {
        editor.replacing = "";
        editor.title = "New skill";
        editor.source = root.skillTemplate;
        editor.open();
    }

    component SkillButton: Rectangle {
        id: button

        property string label: ""

        signal clicked

        border.color: buttonMouse.containsMouse ? "#8ad8ff" : "#3c414a"
        border.width: 1
        color: buttonMouse.pressed ? "#3a3f47" : "#2c3037"
        height: 18
        opacity: button.enabled ? 1 : 0.4
        radius: 3
        width: buttonLabel.implicitWidth + 14

        Text {
            id: buttonLabel

            anchors.centerIn: parent
            color: "#eceff4"
            font.pixelSize: 9
            text: button.label
        }

        MouseArea {
            id: buttonMouse

            anchors.fill: parent
            hoverEnabled: true

            onClicked: button.clicked()
        }
    }

    // Deletes on the second press within a few seconds, never on the first.
    component ConfirmSkillButton: SkillButton {
        id: confirmButton

        property string action: ""
        property bool armed: false

        signal confirmed

        label: confirmButton.armed ? "Press again to " + confirmButton.action.toLowerCase() : confirmButton.action

        onClicked: {
            if (confirmButton.armed) {
                confirmButton.armed = false;
                confirmButton.confirmed();
                return;
            }
            confirmButton.armed = true;
            disarmTimer.restart();
        }

        Timer {
            id: disarmTimer

            interval: 3000

            onTriggered: confirmButton.armed = false
        }
    }

    // A link that reads as one.
    component ReferenceLink: Text {
        color: linkMouse.containsMouse ? "#c4ecff" : "#8ad8ff"
        font.pixelSize: 9
        font.underline: true
        text: "How skills work"

        MouseArea {
            id: linkMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: Qt.openUrlExternally(root.referenceUrl)
        }
    }

    border.color: "#3c414a"
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
        spacing: 6

        // Every skill file, with what it does. A file that failed to load is
        // listed in red, so an added skill that broke can still be fixed or
        // removed from here.
        Column {
            spacing: 4
            width: parent.width

            Text {
                color: "#98a0ac"
                font.pixelSize: 10
                text: "none loaded"
                visible: Skills.skills.length === 0
            }

            Repeater {
                model: Skills.skills

                delegate: Item {
                    id: skillRow

                    required property var modelData

                    height: Math.max(skillText.implicitHeight, skillButtons.height)
                    width: parent ? parent.width : 0

                    Column {
                        id: skillText

                        anchors.left: parent.left
                        anchors.right: skillButtons.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Text {
                            color: skillRow.modelData.loaded ? "#7cff5a" : "#ffb4b4"
                            elide: Text.ElideRight
                            font.bold: true
                            font.pixelSize: 10
                            text: {
                                let origin = "";
                                if (skillRow.modelData.overrides)
                                    origin = "  (edited here)";
                                else if (skillRow.modelData.added)
                                    origin = "  (added here)";
                                return skillRow.modelData.name + origin;
                            }
                            width: parent.width
                        }

                        Text {
                            color: "#98a0ac"
                            font.pixelSize: 8
                            text: skillRow.modelData.description
                            visible: text !== ""
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }
                    }

                    Row {
                        id: skillButtons

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        SkillButton {
                            enabled: Skills.storeReady
                            label: "Edit"

                            onClicked: root.editSkill(skillRow.modelData)
                        }

                        // An edited bundled skill goes back to the bundled
                        // file; an added one is gone.
                        ConfirmSkillButton {
                            action: skillRow.modelData.overrides ? "Revert" : "Remove"
                            visible: skillRow.modelData.added

                            onConfirmed: Skills.uninstall(skillRow.modelData.file)
                        }
                    }
                }
            }
        }

        Row {
            spacing: 8

            SkillButton {
                enabled: Skills.storeReady
                label: "Add a skill"

                onClicked: root.newSkill()
            }

            ReferenceLink {
                anchors.verticalCenter: parent.verticalCenter
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
            border.color: "#8a3a3a"
            border.width: 1
            color: "#3a2326"
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
                color: "#ffb4b4"
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
                color: "#eceff4"
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

                            border.color: "#3c414a"
                            border.width: 1
                            color: mouse.pressed ? "#3a3f47" : "#2c3037"
                            height: 18
                            radius: 3
                            width: 44

                            Text {
                                anchors.centerIn: parent
                                color: "#eceff4"
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
            color: "#3c414a"
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
                        color: "#eceff4"
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
            border.color: "#3c414a"
            border.width: 1
            color: reloadMouse.pressed ? "#3a3f47" : "#2c3037"
            height: 18
            radius: 3
            width: 60

            Text {
                anchors.centerIn: parent
                color: "#eceff4"
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

    // The editor. The text is compiled before it is saved, and a skill that
    // does not compile is never written: the error stays here to be fixed.
    Popup {
        id: editor

        property string problem: ""
        // The added file this text came from, so that renaming the skill
        // replaces the file rather than leaving the old one behind.
        property string replacing: ""
        property alias source: sourceArea.text
        property string title: ""

        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        height: Overlay.overlay ? Overlay.overlay.height * 0.85 : 500
        modal: true
        padding: 12
        parent: Overlay.overlay
        width: Overlay.overlay ? Math.min(Overlay.overlay.width * 0.8, 960) : 640

        Overlay.modal: Rectangle {
            color: "#80000000"
        }
        background: Rectangle {
            border.color: "#3c414a"
            border.width: 1
            color: "#1a1c20"
            radius: 6
        }

        onClosed: editor.problem = ""
        onOpened: sourceArea.forceActiveFocus()

        Item {
            anchors.fill: parent

            Text {
                id: editorTitle

                anchors.left: parent.left
                anchors.top: parent.top
                color: "#eceff4"
                font.bold: true
                font.pixelSize: 14
                text: editor.title
            }

            ReferenceLink {
                anchors.baseline: editorTitle.baseline
                anchors.right: parent.right
                font.pixelSize: 11
            }

            Text {
                id: editorHint

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: editorTitle.bottom
                anchors.topMargin: 4
                color: "#98a0ac"
                font.pixelSize: 10
                text: "Saved as the skill's name in " + Skills.storeDirectory + ". A skill is QML and runs with Kerfur's own rights, so save only what you have read."
                wrapMode: Text.WordWrap
            }

            ScrollView {
                id: sourceScroll

                anchors.bottom: problemBox.visible ? problemBox.top : editorButtons.top
                anchors.bottomMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: editorHint.bottom
                anchors.topMargin: 8

                background: Rectangle {
                    border.color: sourceArea.activeFocus ? "#8ad8ff" : "#3c414a"
                    border.width: 1
                    color: "#121417"
                    radius: 4
                }

                TextArea {
                    id: sourceArea

                    color: "#eceff4"
                    font.family: "monospace"
                    font.pixelSize: 12
                    selectByMouse: true
                    selectedTextColor: "#121417"
                    selectionColor: "#8ad8ff"
                    tabStopDistance: 4 * fontMetrics.averageCharacterWidth
                    wrapMode: TextArea.NoWrap

                    background: null

                    FontMetrics {
                        id: fontMetrics

                        font: sourceArea.font
                    }
                }
            }

            Rectangle {
                id: problemBox

                anchors.bottom: editorButtons.top
                anchors.bottomMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                border.color: "#8a3a3a"
                border.width: 1
                color: "#3a2326"
                height: Math.min(editorProblemText.implicitHeight + 8, 96)
                radius: 3
                visible: editor.problem !== ""

                Text {
                    id: editorProblemText

                    anchors.fill: parent
                    anchors.margins: 4
                    color: "#ffb4b4"
                    elide: Text.ElideRight
                    font.family: "monospace"
                    font.pixelSize: 10
                    text: editor.problem
                    wrapMode: Text.WordWrap
                }
            }

            Row {
                id: editorButtons

                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 8

                SkillButton {
                    height: 24
                    label: "Cancel"

                    onClicked: editor.close()
                }

                SkillButton {
                    height: 24
                    label: "Save"

                    onClicked: {
                        const problem = Skills.install(sourceArea.text, editor.replacing);
                        if (problem === "")
                            editor.close();
                        else
                            editor.problem = problem;
                    }
                }
            }
        }
    }
}
