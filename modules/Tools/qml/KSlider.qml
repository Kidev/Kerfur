import Qt.labs.synchronizer
import QtQuick
import QtQuick.Controls
import qml

Slider {
    id: root

    property string settingName: ""
    property var settingsObject: KSettings

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
