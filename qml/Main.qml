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

    function toggleControls() {
        KSettings.showControls = !KSettings.showControls;
    }

    color: "black"
    visibility: Window.FullScreen
    visible: true

    Shortcut {
        enabled: KSettings.allowEscapeShortcut
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    Shortcut {
        enabled: true
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
        speechRecognizer: face.speechRecognizer
        height: Window.height
        width: 0.15 * Window.width
    }
}
