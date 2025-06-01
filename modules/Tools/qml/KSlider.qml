import QtQuick
import QtQuick.Controls

Slider {
    id: root

    property real decimals: 1
    property bool initialValueSet: false
    property string settingName
    property var settingsObject
    property bool updatingFromSettings: false

    // Don't use a binding for the value property to avoid loops

    // Set the initial value after creation
    Component.onCompleted: {
        root.updatingFromSettings = true;
        root.value = root.settingsObject[root.settingName];
        root.initialValueSet = true;
        root.updatingFromSettings = false;
    }

    // Update the settings when the value changes
    onValueChanged: {
        if (root.initialValueSet && !root.updatingFromSettings) {
            root.settingsObject[root.settingName] = value;
        }
    }

    // Monitor the settings object property with a timer
    // This is a simple polling approach that avoids binding loops
    Timer {
        interval: 100
        repeat: true
        running: root.visible

        onTriggered: {
            if (!root.pressed && root.initialValueSet) {
                var settingValue = root.settingsObject[root.settingName];
                if (Math.abs(settingValue - root.value) > 0.001) {
                    root.updatingFromSettings = true;
                    root.value = settingValue;
                    root.updatingFromSettings = false;
                }
            }
        }
    }
}
