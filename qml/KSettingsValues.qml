import QtCore
import QtQuick

/// Every setting with its default value. `KSettings` is one instance, loaded
/// from and saved to the settings file; its `defaults` is another, never
/// loaded, which keeps the values declared here.
QtObject {
    id: root

    property bool allowEscapeShortcut: true
    /// The longer side the camera frame is reduced to before the vision models
    /// see it. Persisted because it is a property of the machine Kerfur is
    /// running on, not of the session.
    property int analysisMaxSide: 960
    readonly property string appDataPath: StandardPaths.writableLocation(StandardPaths.AppDataLocation)
    property bool autoPaddingEnabled: true
    /// Height of the camera lens above the floor, in metres.
    property real cameraHeight: 1.0
    property real blurMultiplier: 1
    /// The camera chosen in the panel, by device id. Empty means automatic: a
    /// front facing camera, otherwise the system default.
    property string cameraId: ""
    /// The capture size asked of the camera, as `WxH`. A camera that does not
    /// offer it delivers the smallest size that covers it.
    property string captureResolution: "1920x1080"
    /// Tilt of the camera, and of the screen with it, in degrees: 0 looks
    /// straight ahead, 90 straight up, negative values down.
    property real cameraTilt: 0
    /// Distance from the screen, in metres, below which a face makes the eyes
    /// cross, more the closer it comes. 0 turns it off.
    property real crossEyeDistance: 0.5
    /// Whether faces are read for an expression. Off until asked for.
    property bool expressionEnabled: false
    property real eyeMovementRange: 3.0
    /// Score the face detector needs before a face counts.
    property real faceConfidence: 0.6
    property bool faceTrackingDebug: false
    property bool faceTrackingEnabled: false
    property string facesGalleryPath: root.appDataPath + "/faces/"
    /// Horizontal field of view of the camera in degrees, or 0 for the known
    /// figure of a Raspberry Pi camera module (70 for any other camera).
    property real fieldOfView: 0
    property int glowBlendMode: 1
    property real glowBloom: 0
    property real glowBlurAmount: 0
    property color glowColor: "magenta"
    /// The last colour chosen from the colour dialog, kept apart from
    /// `glowColor` so the custom swatch still offers it after a preset is picked.
    property color glowCustomColor: "#ff8000"
    property real glowMaxBrightness: 1.0
    /// Hours of waking time somebody known has to be out of sight before
    /// Kerfur meows at them on their return.
    property real greetAfterHours: 8
    /// Whether Kerfur meows at somebody known who comes back after
    /// `greetAfterHours`.
    property bool greetingEnabled: true
    /// Score the hand landmark model needs before a hand counts.
    property real handConfidence: 0.5
    /// Seconds without anybody in view before the models stop and only
    /// motion is looked for.
    property int idleAfterSeconds: 30
    /// Whether the models stop while nobody is in view.
    property bool idleWatch: true
    /// Cosine similarity a face needs to a gallery sample to be named.
    property real identityThreshold: 0.363
    /// Score one body keypoint needs before the head or a wrist is placed on
    /// it.
    property real keypointConfidence: 0.4
    /// Milliseconds an LED cell takes to come on fully from dark.
    property int ledScreenFadeIn: 0
    /// Milliseconds an LED cell takes to go dark from fully lit.
    property int ledScreenFadeOut: 50
    /// LED cell pitch in pixels. 0 makes every pixel of the eye picture one
    /// cell, as large as the screen allows.
    property int ledScreenLedSize: 0
    /// How bright an LED stripe that is off still looks, from 0 to 1.
    property real ledScreenOffLevel: 0.025
    /// Height of the dark line between two rows of LED cells, as a fraction
    /// of a cell.
    property real ledScreenRowGap: 0.05
    /// Width of the dark line between two LED stripes, as a fraction of a
    /// cell.
    property real ledScreenStripeGap: 0.05
    /// How far a bright LED stripe washes towards white at its core.
    property real ledScreenWhiteCore: 0.22
    property real leftRightAngle: 90
    /// Seconds the eyes stay where the last person was seen before they look
    /// straight ahead.
    property real lookAheadAfterSeconds: 1.5
    /// Whether a stranger's hands are tracked while they are the only person
    /// in view. People the gallery knows always have theirs tracked.
    property bool loneStrangerHands: true
    property real maskSpreadAtMax: 0
    property real maskSpreadAtMin: 0
    property real maskThresholdMax: 1.0
    property real maskThresholdMin: 0.25
    property string modelsPath: root.appDataPath + "/models/"
    /// Score the object detector needs before something near a hand counts.
    property real objectConfidence: 0.35
    /// Score the palm detector needs before a palm counts.
    property real palmConfidence: 0.55
    /// The longer side the preview picture is reduced to before it is shown in
    /// the panel. Separate from the analysis size: this one is what a person
    /// looks at, that one is what the models are shown.
    property int previewMaxSide: 960
    /// Score the pose model needs before something counts as a person.
    property real personConfidence: 0.4
    /// Seconds the models get, after motion woke them, to find somebody
    /// before watching resumes.
    property real probeSeconds: 3
    /// How far below the camera the centre of the screen is, along the
    /// screen, in metres. Negative when the camera is below it.
    property real screenOffset: 0.1
    property bool showControls: false
    /// How many times faster an absence runs while Kerfur is asleep.
    property real sleepTimeFactor: 4
    property bool speechRecognitionDebug: false
    property bool speechRecognitionEnabled: false
    /// How much of the last position the eyes keep when a new one arrives,
    /// from 0 (none) to 1. Applies in full to small steps, which are
    /// measurement jitter, and fades out for large ones, so a real move is
    /// never delayed by it.
    property real trackingSmoothing: 0.5
    property real upDownAngle: 90
    /// How many times a second the camera is looked at: one frame taken, one
    /// pass of every vision model run on it, one picture published. Persisted
    /// for the same reason the two sizes are -- it is a property of the machine
    /// and not of the session -- and it is the setting that decides what Kerfur
    /// costs to keep running.
    property int visionUpdateHz: 6
    /// Share of the picture, in percent, that has to change while watching
    /// for the models to run again.
    property real wakePercent: 1.0
    /// Frames compared a second while watching.
    property real watchHz: 2
}
