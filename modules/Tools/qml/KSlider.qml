import Qt.labs.synchronizer
import QtQuick
import QtQuick.Controls
import qml

// A slider whose value is a setting. The Synchronizer is the whole point: a
// plain two way binding between a control and a persisted property oscillates,
// and every attempt to break the loop by hand ends up dropping an edit.
//
// The look is defined here rather than at each of the twenty call sites. The
// colours are the control panel's; there is no shared palette singleton because
// Hearing and Skills would then have to import Tools, which imports them.
Slider {
    id: root

    property string settingName: ""
    property var settingsObject: KSettings

    implicitHeight: 18

    background: Rectangle {
        color: "#3a3f47"
        height: 4
        radius: 2
        width: root.availableWidth
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight / 2) - (height / 2)

        Rectangle {
            color: "#8ad8ff"
            height: parent.height
            radius: 2
            width: root.visualPosition * parent.width
        }
    }
    handle: Rectangle {
        border.color: "#8ad8ff"
        border.width: 1
        color: root.pressed ? "#ffffff" : "#d7dbe2"
        height: 12
        radius: 6
        width: 12
        x: root.leftPadding + (root.visualPosition * (root.availableWidth - width))
        y: root.topPadding + (root.availableHeight / 2) - (height / 2)
    }

    Synchronizer on value {
        id: sync

        sourceObject: root.settingsObject
        sourceProperty: root.settingName

        onValueBounced: (obj, prop) => {}
        onValueIgnored: (obj, prop) => {
            try {
                const srcObj = root.settingsObject;
                const propName = root.settingName;
                // If either side is missing, nothing to do.
                if (!srcObj || !propName)
                    return;

                const srcVal = srcObj[propName];
                const dstVal = (obj === root && prop === "value") ? root.value : (obj && prop in obj ? obj[prop] : undefined);

                if (dstVal !== srcVal) {
                    console.log("qml:", "[", obj.objectName || propName, "]", "Value ignored for", prop, "on", obj);
                }
            } catch (e) {}
        }
    }
}
