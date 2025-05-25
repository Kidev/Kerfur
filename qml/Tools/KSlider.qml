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
        updatingFromSettings = true;
        value = settingsObject[settingName];
        initialValueSet = true;
        updatingFromSettings = false;
    }

    // Update the settings when the value changes
    onValueChanged: {
        if (initialValueSet && !updatingFromSettings) {
            settingsObject[settingName] = value;
        }
    }

    // Monitor the settings object property with a timer
    // This is a simple polling approach that avoids binding loops
    Timer {
        interval: 100
        repeat: true
        running: root.visible

        onTriggered: {
            if (!root.pressed && initialValueSet) {
                var settingValue = settingsObject[settingName];
                if (Math.abs(settingValue - root.value) > 0.001) {
                    updatingFromSettings = true;
                    root.value = settingValue;
                    updatingFromSettings = false;
                }
            }
        }
    }
}
