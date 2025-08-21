// modules/Tools/qml/KSlider.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.synchronizer

Slider {
    id: root

    // === Public API ===
    // Name of the property in settingsObject to synchronize with (e.g. "ledScreenLedSize").
    property string settingName: ""
    // Object holding the property (e.g. KSettings instance).
    property var settingsObject: null

    // Optional tuning, leave as-is if your uses already set from/to/step/etc. outside.
    // Do NOT set a default "value:" here. Initial value comes from the model via Synchronizer.

    // Make instance names visible in logs
    // (many instances already set objectName from outside; keep this as-is if you prefer)
    // objectName: settingName

    // Two-way sync between slider.value and settingsObject[settingName]
    // Initial sync source is the model (sourceObject/sourceProperty), per Synchronizer rules.
    Synchronizer on value {
        id: sync
        sourceObject: root.settingsObject
        sourceProperty: root.settingName

        // NOTE:
        // "valueIgnored" happens when target setter doesn't emit a change signal.
        // In QML, that's normal for no-op assignments (same value), so we silence those.
        onValueIgnored: (obj, prop) => {
            try {
                const srcObj = root.settingsObject
                const propName = root.settingName
                // If either side is missing, nothing to do.
                if (!srcObj || !propName) return;

                const srcVal = srcObj[propName]
                const dstVal = (obj === root && prop === "value") ? root.value
                : (obj && prop in obj ? obj[prop] : undefined)

                // Only log if it *really* looks like a problematic ignore (value differs)
                // No-op assigns (equal values) are expected and silenced.
                if (dstVal !== srcVal) {
                    console.log("qml:", "[", obj.objectName || propName, "]",
                                "Value ignored for", prop, "on", obj)
                }
            } catch (e) {
                // Fallback: never crash on logging
            }
        }

        onValueBounced: (obj, prop) => {
            // "Bounced" means it tried to echo back; not an error.
            // Keep silent by default; uncomment if you need to debug real loops.
            // console.log("qml:", "[", obj.objectName || root.settingName, "]",
            //             "Value bounced for", prop, "on", obj)
        }
    }
}
