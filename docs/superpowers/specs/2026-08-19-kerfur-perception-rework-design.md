# Kerfur perception rework: design

Date: 2026-08-19
Status: approved, awaiting implementation plans

## 1. Context and goals

Kerfur is a Qt 6 / QML robot face. Today it runs YuNet face detection through
OpenCV DNN on the CPU and whisper.cpp for speech, both on the main application
process, and it dies after roughly a day of uptime with what looks like an out of
memory kill.

This rework replaces the perception stack with hardware accelerated inference on
a Hailo accelerator, replaces speech recognition with Moonshine, adds a Home
Assistant integration, adds a behaviour layer that turns perception into
personality, and makes the process survive indefinitely.

The goals, in the order they constrain the design:

1. The process runs for weeks without growing. This is a design constraint on
   every queue, buffer and foreign handle, not a bug hunt to be done at the end.
2. Perception has exactly one owner. Today it is spread across two QML modules
   that the display layer imports directly, which is why there is no single place
   to look at what Kerfur currently believes about the world.
3. The Raspberry Pi is the real target. The desktop build exists so the face and
   the control panel can be iterated without the hardware, and it carries no AI
   at all.
4. Camera orientation is a first class runtime concern, not a build time
   assumption. The camera sometimes presents upside down, and the detector must
   recover from that on its own.

Non goals for this rework: text to speech (Moonshine ships it, and the library
integration is designed so it can be added later without restructuring), the
WebAssembly build gaining any new capability, and any change to the visual
features of the face.

## 2. Hardware and platform targets

The deployment target is a single Raspberry Pi 5 with 16 GB of RAM, an AI HAT+
carrying a Hailo accelerator rated at 40 TOPS, a Raspberry Pi Camera Module v3
with the 120 degree wide angle lens, a USB microphone, and an integrated screen.
Everything runs in one process on that one board. There is no second machine and
no message bus in the perception path.

The accelerator variant is resolved at runtime rather than assumed. The Hailo
model zoo publishes different compiled binaries for `hailo8`, `hailo8l` and
`hailo10h`, and the same Kerfur binary must work on whichever is present, so the
device architecture is queried through HailoRT at startup and the model manifest
is indexed by it.

Three platform configurations exist:

| Configuration | Modules built | Purpose |
|---|---|---|
| `pi` | Core, Vision, Hearing, HomeAssistant, Behaviour, Display, Tools | the product |
| `desktop` | Core, HomeAssistant, Behaviour, Display, Tools | development of the face and the control panel |
| `wasm` | Core, Behaviour, Display, Tools | the existing web demo, unchanged in capability |

The desktop configuration carries no inference of any kind. There is no stub
detector, no CPU fallback model and no synthetic person generator in the shipped
build. `WorldModel` simply stays empty, and the face falls back to its idle
behaviour, which is a state the behaviour engine has to handle correctly anyway.

## 3. Architecture

### 3.1 The problem with the current shape

`Display` and `Tools` import `Vision` and `Hearing` and instantiate `FaceTracker`
and `SpeechRecognizer` as QML children (`modules/Display/qml/Face.qml` lines 7,
8, 282 and 300; `modules/Tools/qml/ControlPanel.qml` lines 4, 5 and 328). The
consumers own the producers. That coupling is what makes an AI free build
impossible, and it means perception state lives in whichever C++ object happened
to produce it.

### 3.2 The inversion

Producers push into a shared model. Consumers read the model. Nothing imports a
producer.

```
                       modules/Core            always built, no AI dependencies
                         WorldModel            QML singleton, sole perception truth
                         PeopleModel           QAbstractListModel of tracked people
                            ^   ^   ^
        producers push      |   |   |       consumers read
    +-----------------------+   |   +------------------------+
    |                           |                            |
modules/Vision  [pi]     modules/Hearing [pi]        modules/Behaviour [always]
  libcamera + HailoRT      Moonshine streaming STT     state machine, gaze, mood
                                                                |
modules/HomeAssistant [pi, desktop]                             | drives
  publishes WorldModel + Behaviour state                        v
  subscribes commands ------------------------------>  modules/Display [always]
                                                         Face, LedScreen, Glow
```

`Vision` and `Hearing` are instantiated at the application root and are not
importable QML types for anyone else. Removing them from a build is an
`add_subdirectory` conditional plus one import that is not there. No feature
macro leaks into logic.

### 3.3 Threading

Three long lived threads beyond the GUI thread on the Pi:

- the libcamera dispatch thread, owned by libcamera,
- the inference worker thread, which owns the Hailo `VDevice` and every model
  bound to it,
- the audio thread, which owns the Moonshine stream.

Every crossing into the GUI thread is a queued connection carrying one immutable
value. The GUI thread never blocks on a device, and no device thread ever touches
a QML exposed object directly.

## 4. Core module

Built on every platform. Depends on QtCore, QtQml and QtGui only.

### 4.1 Types

`PerceptionFrame` is the immutable value that crosses the thread boundary. One
signal per processed frame carries one of these. It holds the frame timestamp,
the sequence number, the resolved camera orientation, and the vector of
`TrackedPerson`.

`TrackedPerson` holds the track identity, the bounding box in normalized
coordinates, the seventeen COCO keypoints with per keypoint confidence, an
estimated distance, the recognised identity with its confidence, the current
emotion label with its confidence, and the list of held object labels.

`WorldModel` is the QML singleton. It exposes the aggregate that the face, the
behaviour engine and Home Assistant all read: whether anyone is present, how
many, the nearest person, the last final transcript line, whether the wake word
is active, whether the camera and microphone are online, and the current camera
orientation.

`PeopleModel` is the `QAbstractListModel` behind the per person detail, so the
control panel and any future debug overlay can show all tracks without the
singleton growing a variant list property.

The critical property of this design is that a frame update is one signal
emission and one model reset or targeted row change, not one emission per changed
scalar. The current `FaceTracker` emits `faceCenterChanged`,
`facePixelCenterChanged`, `facePixelSizeChanged`, `faceLandmarksChanged` and
`cameraFrameChanged` separately per frame, and exposes `cameraFrameBase64` as a
bound property that re encodes a full resolution image on every one of them.

### 4.2 BoundedQueue

One template, used by every producer and consumer pair in the process: the camera
to inference queue, the audio ring, and the Home Assistant outbound queue. Fixed
capacity chosen at construction, drop oldest on overflow, and a high water mark
counter that `ResourceMonitor` reports.

No buffer anywhere in Kerfur grows with input rate. This single rule removes the
entire class of failure that `WhisperEngine::m_audioBuffer` and
`SpeechRecognizer::m_audioBuffer` represent today, where a producer that outruns
its consumer grows a `std::vector<float>` and a `QByteArray` without bound.

### 4.3 ResourceMonitor

Samples every sixty seconds and both logs a line and publishes to Home Assistant:
resident set size from `/proc/self/statm`, open file descriptor count from
`/proc/self/fd`, thread count, live instance counts for the hot types, and every
registered queue's high water mark.

The point is that a leak becomes a visible slope in a Home Assistant history
graph rather than something reconstructed after an overnight kill.

### 4.4 Foreign handle ownership

Every handle from a C library gets a move only RAII wrapper in the module that
owns it, and no raw handle escapes one. This covers HailoRT `VDevice`,
`InferModel` and `ConfiguredInferModel`, libcamera `Camera`, `Request` and
`FrameBuffer`, the paho MQTT client, and Moonshine's opaque `int32_t` handles.

Moonshine needs specific care: its handles are released with
`moonshine_free_transcriber` and `moonshine_free_stream`, and any buffer it
returns must go back through `moonshine_free_buffer` rather than the process
`free`, which its documentation calls out explicitly.

## 5. Vision module

Built on the `pi` configuration only.

### 5.1 Capture

libcamera's C++ API directly. Qt Multimedia's V4L2 backend does not drive the
IMX708 sensor in the Camera Module v3 properly, so `QCamera` has no role on the
Pi. Two streams are configured from one request:

| Stream | Size | Format | Purpose |
|---|---|---|---|
| lores | 640x640 letterboxed | RGB888 | detector input for pose and object detection |
| main | 1536x864 | RGB888 | source for face and hand crops |

Crops come from the high resolution stream. Running ArcFace against a face region
extracted from a 640 pixel wide frame produces embeddings that will not separate
one person from another, and recognising specific people is a requirement, so the
extra stream is not optional.

### 5.2 Orientation

`CameraConfiguration::orientation` carries the correction. The IMX708 applies a
180 degree rotation in the sensor at no cost, which is the case that actually
occurs. 90 and 270 degrees are not free on this sensor and require a rotation
stage before inference; they are supported but documented as costing a copy.

Correction is automatic through two independent mechanisms, because either one
alone has a blind spot:

- Geometry check. COCO pose keypoints give the nose as index 0 and the hips as
  indices 11 and 12. If the median nose y coordinate sits below the median hip y
  coordinate across a run of consecutive detections, the image is inverted.
  This is precise, but it needs detections to work with.
- Blind probe. If no person has been detected for a configured interval while the
  camera is delivering frames, flip 180 degrees and try again. This covers the
  case where the inversion suppresses detection well enough that the geometry
  check never receives data.

The resolved orientation is exposed on `WorldModel` and is manually overridable
from the control panel, which wins over the automatic mechanisms until it is
returned to automatic.

### 5.3 Inference graph

One `VDevice`, with every model configured against it under a shared `group_id`.
This is what the second `HailoInfer` constructor in `hailo-apps` exists for and
is how several models share one accelerator without contending for it.

| Stage | Model | Cadence | Input | Produces |
|---|---|---|---|---|
| A | `yolov8s_pose` | every frame | 640x640 lores | person boxes and 17 keypoints |
| B | `yolov8s` | every third frame | 640x640 lores | COCO 80 class objects |
| C | `scrfd_10g` | close persons | head crop from keypoints | face box and 5 landmarks |
| D | `arcface_mobilefacenet` | unconfirmed tracks | 112x112 aligned face | 512 dimension embedding |
| E | `clip_vit_b_32_image_encoder` | close persons, about 2 Hz | 224x224 face and hand crops | emotion and held object semantics |

"Close" is a Vision level proximity threshold on the person's bounding box height
as a fraction of frame height, and it exists to keep stages C through E off people
too small in frame for the result to mean anything. It is distinct from the
`Attentive` to `Engaged` threshold in section 8, which is a behavioural distance
and is tuned separately.

The zoo default for pose on this device is `yolov8m_pose`. Stage A pins the `s`
variant instead, because five models share one accelerator here and the medium
variant spends headroom that stages C through E need. Whether the device has room
for the medium variant once the graph is real is measured in P2, not guessed now.

Stage C runs against a head crop derived from the pose keypoints rather than the
whole frame. It is both cheaper and more reliable than whole frame face
detection, because the pose stage has already told it where a head is.

Stage D aligns the face onto the standard ArcFace reference points with a
similarity transform derived from the five landmarks before embedding it.

### 5.4 Identity

The gallery is the set of images in `assets/faces`, which currently holds
`kidev.jpg`. On first run each is embedded and the result is written to a local
embedding store, keyed by a hash of the source image so that changing or adding a
face re-enrols only what changed.

Matching is cosine distance against the gallery, with hysteresis and an N of M
vote held per track rather than per frame. A track becomes a named person only
after it has agreed with itself several times, and it stops being one only after
it has disagreed several times. Without this the identity sensor oscillates and
every downstream consumer, Home Assistant included, sees noise.

### 5.5 Emotion and held objects

There is no facial expression model in the Hailo model zoo. CLIP is, and it
solves both semantic questions with one image encoder.

At startup the text encoder embeds the configured prompt sets once, the results
are cached to disk keyed by the prompt set, and the text encoder is released.
Thereafter only the image encoder runs: the aligned face crop is embedded and
matched by cosine distance against the emotion prompt set, and the crop around
the wrist keypoints is embedded and matched against the object prompt set.

Prompt sets live in configuration. Tuning what "a happy face" or "a person
holding a mug" means is a config edit, not a rebuild.

Held objects also come from stage B, which gives exact COCO labels for the eighty
classes it knows. The two sources are merged: a COCO detection whose box overlaps
a wrist keypoint is authoritative for its label, and CLIP fills in what COCO has
no class for.

### 5.6 Tracking

A small tracker inside the Vision module: intersection over union combined with
keypoint distance, greedy assignment, constant velocity prediction across frames,
and birth and death counts so a track has to be seen a few times before it exists
and missed a few times before it does not.

It is about two hundred lines, has no dependency, and is unit testable on the
desktop configuration against recorded detections. Hailo ships a tracker, but it
lives in the GStreamer and TAPPAS stack that this design deliberately does not
use.

Tracking is what makes identity and emotion stable, and it is what lets the
person count published to Home Assistant be a number rather than a flicker.

### 5.7 Frame flow

```
libcamera thread
     |
     |  BoundedQueue<Frame>, depth 2, drop oldest
     v
inference worker thread
     owns VDevice and every HailoInfer
     runs stages A through E at their cadences
     runs the tracker
     |
     |  one queued connection, one PerceptionFrame value
     v
GUI thread -> WorldModel
```

Dropping oldest rather than blocking is deliberate. A perception pipeline that
cannot keep up should show the most recent view of the world, not an ever
growing backlog of stale ones.

The debug preview that the control panel shows is produced only while
`faceTrackingDebug` is on, at a reduced rate, and delivered through a
`QVideoSink`. There is no base64 encoded image property anywhere in the new
design.

## 6. Hearing module

Built on the `pi` configuration only.

Moonshine ships prebuilt shared libraries for arm64 Linux with a C ABI in
`moonshine-c-api.h`. The module links that rather than embedding a Python
runtime.

The streaming C API is used directly, not `MicTranscriber`. Kerfur already owns
the microphone through `QAudioSource`, the control panel already has a device
picker, and running two audio stacks in one process is how device selection
becomes unpredictable.

```
QAudioSource
     |
     |  fixed capacity ring buffer, drop oldest
     v
moonshine_transcribe_add_audio_to_stream
moonshine_transcribe_stream
     |
     +-- onText: partial hypothesis, never published, never treated as final
     +-- onLine: finished segment -> WorldModel::heardText
```

The wake word is `moonshine_transcriber_set_keyterms` with "kerfur" and its
plausible spellings. This is a real bias applied inside the decoder, replacing
the current approach of matching the substrings "kerf", "fur", "care fur" and
"curve fur" against whisper output (`WhisperEngine.hpp` line 83).

Two constraints from Moonshine's own documentation that the design has to
respect: keyterms require a streaming model architecture and are rejected by the
Tiny and Base models, so a streaming architecture is pinned; and models are
OnnxRuntime flatbuffers with the `.ort` extension, not `.onnx`.

`load()` downloads models into a cache on first use. Kerfur must boot without a
network, so the models are pre staged into the cache by the model provisioning
step described in section 10.

## 7. HomeAssistant module

Built on every configuration that has sockets, which is `pi` and `desktop`.

`paho-mqtt-c` in its asynchronous mode, wrapped so that its callbacks, which
arrive on its own thread, are marshalled onto the Qt event loop. One `HaBridge`
QML singleton is the whole public surface.

### 7.1 Availability

A retained last will and testament on `kerfur/availability`, with `offline` as
the will and `online` published on connect. Given the failure this rework exists
to fix, it matters that when Kerfur dies Home Assistant marks its entities
unavailable rather than serving the last value it saw indefinitely.

### 7.2 Discovery

Retained configuration payloads under
`homeassistant/<component>/kerfur/<object_id>/config`, every one of them carrying
the same `device` block so the entities group under a single Kerfur device.

| Entity | Source |
|---|---|
| `binary_sensor.kerfur_presence` | tracker, occupancy device class |
| `sensor.kerfur_person_count` | tracker |
| `sensor.kerfur_identity` | ArcFace, nearest track |
| `sensor.kerfur_emotion` | CLIP, nearest track |
| `sensor.kerfur_held_object` | COCO merged with CLIP |
| `sensor.kerfur_heard` | Moonshine final lines |
| `sensor.kerfur_state` | behaviour engine state |
| `sensor.kerfur_rss_mb` | ResourceMonitor, diagnostic |
| `sensor.kerfur_fps` | Vision, diagnostic |
| `sensor.kerfur_uptime` | ResourceMonitor, diagnostic |

The three diagnostic entities are deliberate. They put the memory trend on a
graph the owner already looks at.

### 7.3 Commands

Subscribed on `kerfur/cmd/#`: `express` to force an expression, `look` to aim the
gaze, `meow`, `sleep` and `wake`. There is no `speak` command, because text to
speech is out of scope for this rework.

### 7.4 Backpressure

Telemetry publishes at QoS 0; discovery and availability publish at QoS 1
retained. Every topic publishes on change with a one second minimum interval
between publications.

The outbound queue is a `BoundedQueue` and drops telemetry while the broker is
unreachable. An MQTT client that buffers indefinitely across a broker outage is
itself the leak this rework is meant to eliminate, so it is bounded by
construction.

## 8. Behaviour module

Built on every platform, because the face needs it and the desktop build needs
the face to work.

A C++ `BehaviourEngine` implemented over `QStateMachine`, exposing its current
state as an enum property. It reads `WorldModel` and writes a small output
surface that `Face.qml` binds to. Face rendering stops making decisions.

```
  Sleeping --presence--> Idle --person--> Attentive --close--> Engaged
     ^                    |                  |                   |
     +--no presence-------+------------------+-------------------+
                          |
      known identity --> Greeting --> Engaged
      wake word      --> Listening
      command or held object change --> Reacting, transient
```

Outputs: a normalized gaze target, an expression, blink and wink triggers, a
sound cue and an LED intensity.

`Sleeping` also lowers the inference cadence. That saves power and heat, and it
reduces the amount of machinery running overnight, which is precisely when the
current build dies.

This module absorbs a control loop that currently lives in QML. `Face.qml` lines
320 to 353 run eye angle arithmetic, a smoothing factor and a centre return timer
inside a declarative language. That arithmetic moves into C++ where it can be
tested, and the `FaceTracker` and `SpeechRecognizer` blocks in `Face.qml` (lines
282 to 296 and 300 to 353) collapse into bindings against the behaviour engine.

## 9. Display and Tools

The visual features are correct and are not being changed. The work here is
lifetime and leak auditing, plus repointing the bindings.

Audit targets:

- `ShaderEffectSource` lifetimes in `Glow.qml`, `BlurHelper.qml` and
  `LedScreen.qml`. A source with `live` true and `recursive` set is a known way
  to grow GPU memory without bound.
- The `SoundEffect` at `Face.qml` line 277, confirming it is not recreated per
  playback.
- Any dynamic object creation, and image cache behaviour for the assets under
  `assets/img`.

`ControlPanel.qml` loses its `required property FaceTracker faceTracker` and its
inline `SpeechRecognizer`, and gains bindings against `WorldModel` and
`BehaviourEngine` plus the camera orientation override control.

## 10. Build system and dependencies

`KERFUR_PLATFORM` takes `pi`, `desktop` or `wasm`, auto detected from
`CMAKE_SYSTEM_PROCESSOR` together with the presence of `libhailort`, and
overridable on the command line.

`vcpkg.json` drops `whisper-cpp` and `ggml`, adds `paho-mqtt-c`, and keeps
`opencv`. OpenCV stays because the crop, letterbox, resize and `warpAffine`
operations that the Hailo pre and post processing needs are all it is used for,
and re implementing them buys nothing.

New external dependencies not in vcpkg, resolved on the Pi: HailoRT and its
headers, libcamera, and the Moonshine prebuilt shared library.

Model provisioning replaces `assets/models.txt`, which currently lists two ONNX
files and a whisper binary that this rework removes. The replacement is a
manifest that lists, per device architecture, the compiled Hailo binaries and
their checksums, plus the Moonshine `.ort` models, and a `make models` target
that fetches and verifies them into the application data directory. Kerfur boots
offline, so nothing is downloaded at run time.

Two corrections while in here: `.env` pins `QT_ROOT_DIR` to Qt 6.10.0 while the
installed toolchain is 6.11.1, and `.clang-format` sets
`SpaceBeforeCpp11BracedList` to true, which produces `int i {0}` rather than the
required `int i{0}`.

## 11. Memory and stability

The failure to be designed out is unbounded growth ending in an out of memory
kill after roughly a day. Five mechanisms, all of which are part of the first
sub-project rather than a later cleanup:

1. `BoundedQueue` everywhere, as specified in section 4.2. No buffer grows with
   input rate.
2. RAII on every foreign handle, as specified in section 4.4. No raw handle
   escapes a wrapper.
3. `ResourceMonitor`, as specified in section 4.3, reporting to both the log and
   Home Assistant.
4. A `make soak` target that runs the process headless against a synthetic frame
   source that lives in the test harness and is never linked into a shipped build and asserts that the resident set size slope stays under a threshold.
   It runs on the desktop configuration under AddressSanitizer and
   LeakSanitizer, which needs no Hailo hardware and is therefore runnable in CI,
   and it runs for a real twenty four hours on the Pi as the definition of done.
5. A systemd unit with `Restart=always`, `WatchdogSec` and `MemoryMax`, so that a
   leak nobody found degrades into a restart instead of an out of memory kill.

## 12. Style and conventions

All C++, QML and JavaScript follows the official Qt conventions as applied in the
SynQt project, with the same deliberate exceptions.

- Brace initialization everywhere, written without a space: `int a{0};`,
  `QString name{QStringLiteral("kerfur")};`.
- Every control statement is braced, including single statement bodies.
- Never a C-style cast, and never the constructor form `T(x)` on a single
  expression. Every conversion is an explicit `static_cast`, or `qobject_cast`,
  or at a C API boundary `reinterpret_cast` or `const_cast`.
- No exceptions and no RTTI in Kerfur's own code. HailoRT returns `Expected<T>`,
  Moonshine, libcamera and paho return error codes, and all of them suit this.
  OpenCV throws, so its exceptions are caught at the call boundary and converted
  to an error return there and nowhere else.
- Every `QObject` subclass carries `Q_OBJECT`. Reimplemented virtuals use
  `override` and drop `virtual`.
- C++ files are named after the single primary class they contain, in lower case:
  `worldmodel.h` and `worldmodel.cpp`. QML files keep the PascalCase name of the
  type. Includes run from the paired header, to module, to Qt, to the standard
  library.
- Every source file opens with its SPDX header in the file's comment syntax:
  `SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux` and
  `SPDX-License-Identifier: AGPL-3.0-or-later`.
- Comments explain why a thing is the way it is, and what breaks otherwise. They
  do not restate the signature they sit above.
- Plain ASCII throughout, in code, comments, documentation and commit messages.
  No emoji.
- Commit messages are a single capitalized line with no body and no trailers.
  Work happens on `main`, and commits are never pushed.

## 13. Sub-project sequence

Each sub-project gets its own implementation plan. The first one carries the
architecture, so nothing else can start before it lands.

### P1: Foundation

The Core module, the dependency inversion, the platform split in the build
system, the memory hygiene infrastructure, the Display audit, and the style and
configuration corrections.

Done when: the desktop configuration builds and runs with no AI and a face that
behaves correctly against an empty `WorldModel`; the Pi configuration builds with the new
`Vision` and `Hearing` modules present as empty producers; `Face.qml` reads `WorldModel` and
`BehaviourEngine` rather than `FaceTracker` and `SpeechRecognizer`; and the old
Vision and Hearing implementations are deleted rather than mutated.

### P2: Vision on Hailo

libcamera capture with orientation handling, the HailoRT inference worker, stages
A through E, the tracker and the identity store.

Done when: a person in front of the camera produces stable tracks with keypoints;
a face in `assets/faces` is recognised by name without flicker; an emotion label
and a held object label appear for a close person; and the camera recovers from
being mounted upside down without intervention.

### P3: Hearing on Moonshine

Streaming speech to text with keyterm biasing.

Done when: speech produces final transcript lines in `WorldModel`, and saying
"kerfur" raises the wake word.

### P4: Home Assistant

The MQTT bridge, discovery, telemetry and commands.

Done when: a Kerfur device appears in Home Assistant with all its entities
populated, the entities go unavailable when the process stops, and a command
published to `kerfur/cmd/express` changes the face.

### P5: Behaviour

The personality state machine.

Done when: Kerfur greets a recognised person by transitioning through `Greeting`,
tracks the nearest person's head while `Attentive`, mirrors emotion while
`Engaged`, and drops to `Sleeping` with reduced inference cadence when nobody has
been present for the configured interval.

## 14. Open items to pin during implementation

These are known unknowns with a known place to resolve them, not unresolved
design questions.

- The exact source URLs and checksums for the compiled Hailo model binaries.
  `hailo-apps` resolves them through its own Python installer; the URL pattern
  will be extracted and a verified manifest committed, so Kerfur never depends on
  that installer at build or run time. Resolve in P2.
- The Moonshine streaming model architecture to pin, and the on disk cache layout
  its loader expects, so the models can be pre staged. Resolve in P3.
- Whether the 90 and 270 degree rotation path is needed at all in practice, or
  whether 0 and 180 cover every mounting the hardware actually sees. Resolve in
  P2 against the real enclosure.
- The distance threshold that separates `Attentive` from `Engaged`, which is a
  function of the 120 degree lens and the screen's mounting height, and is a
  tuning value rather than a design decision. Resolve in P5.
