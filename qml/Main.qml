import Display
import QtQuick
import QtQuick.Window
import Tools
import qml 1.0

Window {
    id: root

    function requestExit() {
        Qt.quit();
    }

    // Whether a text field or the skill editor has the keyboard. Esc and Tab
    // belong to it then: Esc closes the editor instead of quitting.
    function typing() {
        return root.activeFocusItem instanceof TextInput || root.activeFocusItem instanceof TextEdit;
    }

    function toggleControls() {
        KSettings.showControls = !KSettings.showControls;
    }

    color: "black"
    visibility: Window.FullScreen
    visible: true

    Shortcut {
        enabled: KSettings.allowEscapeShortcut && !root.typing()
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    Shortcut {
        enabled: !root.typing()
        sequence: "Tab"

        onActivated: root.toggleControls()
    }

    Face {
        id: face

        anchors.fill: parent
        visible: true

        onHeld: root.toggleControls()
    }

    ControlPanel {
        id: controlPanel

        anchors.right: parent.right
        anchors.top: parent.top
        faceTracker: face.faceTracker
        holdToToggleMs: face.holdDuration
        speechRecognizer: face.speechRecognizer
        height: Window.height
        width: 0.15 * Window.width
    }
}
