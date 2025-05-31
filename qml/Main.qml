import QtQuick
import QtQuick.Window
import Display
import Tools

Window {
    id: root

    readonly property bool allowEscapeShortcut: true

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
        enabled: root.allowEscapeShortcut
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
        height: parent.height
        settings: settings
        width: 150
    }
}
