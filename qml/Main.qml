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

    Shortcut {
        enabled: KSettings.allowEscapeShortcut
        sequence: "Esc"

        onActivated: root.requestExit()
    }

    Shortcut {
        enabled: true
        sequence: "Tab"

        onActivated: KSettings.showControls = !KSettings.showControls
    }

    Face {
        id: face

        anchors.fill: parent
        visible: true
    }

    ControlPanel {
        id: controlPanel

        anchors.right: parent.right
        anchors.top: parent.top
        faceTracker: face.faceTracker
        height: Window.height
        width: 0.15 * Window.width
    }
}
