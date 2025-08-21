import Display
import QtQuick
import QtQuick.Window
import Tools

Window {
    id: root

    function requestExit() {
        Qt.quit();
    }

    color: "black"
    visibility: Window.FullScreen
    visible: true

    KSettings {
        id: settings

    }

    Shortcut {
        enabled: settings.allowEscapeShortcut
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    Shortcut {
        enabled: true
        sequence: "Tab"

        onActivated: settings.showControls = !settings.showControls
    }

    Face {
        id: face

        anchors.fill: parent
        settings: settings
        visible: true
    }

    ControlPanel {
        id: controlPanel

        anchors.right: parent.right
        anchors.top: parent.top
        faceTracker: face.faceTracker
        height: parent.height
        settings: settings
        width: 150
    }
}
