# Kerfur  
[Try it in your browser](https://demo.kidev.org/Kerfur)

[![meow](assets/kerfur.gif)](https://demo.kidev.org/Kerfur)  

## How to install ?
You can use an installer with easy updates:
- [Windows x64](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Windows-x64.exe)  
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Linux-x64)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/KerfurInstaller-Linux-arm64-rpi)  

You can download and install a .app on macOS
- [macOS 15 arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-macos-15-arm64.tar.gz)  

You can download and run an AppImage:
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-x64.AppImage)  
- [Linux arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-arm64.AppImage)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-bookworm-arm64-rpi.AppImage)  

You can download an archive and extract the files:
- [Windows x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-windows-x64.zip)  
- [Windows arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-windows-arm64.zip)  
- [Linux x64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-x64.tar.gz)  
- [Linux arm64](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-linux-arm64.tar.gz)  
- [Linux arm64 (RaspiOS Bookworm for RPI5)](https://github.com/Kidev/Kerfur/releases/latest/download/Kerfur-bookworm-arm64-rpi.tar.gz)  

## How to build ?
You can also chose to build the project. It is very straightforward.  

### Requirements
- Qt 6.7.0+ installed and its install path known  
- Standard C++ build packages (`make`, `cmake`, `gcc` or `clang` or `msvc`...).  
- An internet connection, it will download `vcpkg` and if required `emsdk`

### Build
Run the following, replacing the value of `QT_ROOT_DIR` with yours. You must include the version and the architecture:  
```console
kidev:~$ make QT_ROOT_DIR=/opt/Qt/6.8.3/gcc_64
````

### Advanced
- You must include the version and the architecture like I did.  
- You can also simply run `make` if you have the env var `QT_ROOT_DIR` set, or if you have a `.env` file at the root of the repository that exports `QT_ROOT_DIR`.  
- You can use `make` rules included to:  
    - `make installer` to create an online installer using a FTP as CDN. You will need to provide credentials (`CDN_UPLOAD_SOCKET` `CDN_UPLOAD_USERNAME` `CDN_UPLOAD_PASSWORD`). The repository for the OS you built on will be created inside a folder named after your OS, in the folder you are in upon connection.  
    - `make web` to create a WASM build  
    - `make run-web` to start a localserver running the application (requires to have a WASM build first with `make web`)  
    - `make upload-web` to upload the WASM application to a web server. You will need to provide credentials (`WASM_UPLOAD_SOCKET` `WASM_UPLOAD_USERNAME` `WASM_UPLOAD_PASSWORD`). The application will be uploaded inside a folder `Kerfur` in the folder you are in upon connection.
    - `make models` to fetch and checksum verify the perception models into the application data directory, which is where a desktop build looks for them
    - `make models-pi` to fetch the Raspberry Pi profile, which is the same set as the desktop one
    - `make models-web` to fetch the smaller web profile into `models/` in the source tree, which `make web` bakes into the WebAssembly file system

### Perception
Kerfur sees and hears with models that are provisioned separately from the binary, because it has to boot without a network. `assets/models.json` is the manifest: it pins every file by SHA-256, and `make models` is what reads it. The desktop profile covers the five vision stages, speech recognition and speaker recognition; the web profile drops the ones a browser cannot afford.

The Pi profile is the same set as the desktop one, because until the accelerator backend lands the Pi runs exactly the same CPU graph -- every stage above goes through OpenCV DNN and Whisper, neither of which is desktop-only. It exists as a separate profile name so that the day the two diverge it is a change to `assets/models.json` rather than to any code.

Without them the face still runs; it simply has nothing to look at. Any stage whose model file is missing is disabled and logged at startup rather than failing the start.

**How much of the frame the models get to see** is the one knob worth turning when detection is unreliable. The camera frame is reduced before any model runs -- to 960 pixels on the longer side by default -- and everything downstream sees only what survives that. A face across the room or a hand at arm's length is a few dozen pixels wide by then, which is where the detail goes. *Vision > Analysis detail* in the control panel raises it as far as 1920; the cost is superlinear and is printed next to the setting, in milliseconds per pass, so the trade is made against a number rather than a feeling. It is never enlarged past what the camera actually delivers, so if the panel reports a small camera resolution that is the thing to fix first.

**The picture on the panel is a separate setting.** *Vision > Preview detail* is what a person looks at; *Analysis detail* is what the models are shown, and the two are paid for differently -- the analysis is one inference chain per frame, the preview is a resample and a texture upload. The preview is capped at 960 pixels on the longer side by default and, like the analysis frame, is never enlarged past what arrives. The panel prints the size it is actually drawing at, beside the size the camera is actually delivering, because a preview that names one resolution while showing a picture built from a quarter as many pixels is indistinguishable from a broken camera.

**How often any of it happens** is *Vision > Update rate*, and it is the setting that decides what Kerfur costs to leave running. One tick is one frame taken, one pass of every model run on it and one picture published: six times a second by default, because nothing being watched for -- an expression, a grip, somebody crossing a room -- happens faster than that, and every pass beyond it is a whole inference chain spent to redraw the same answer. The camera format is then chosen to cover that rate rather than to be as fast as the device can manage: a webcam will happily offer sixty frames a second, of which fifty-four would be decoded, converted and thrown away.

The picture is held back until the pass measured on it comes home, so the boxes are always drawn on the frame they were measured in rather than on a later one.

### Knowing people
`identities/` is the gallery of people Kerfur recognises, and `identities/ids.json` is what declares them:

```json
{
    "kidev": {
        "name": "kidev",
        "face": "faces/kidev.png",
        "voice": "voices/kidev.mp3",
        "priority": 0,
        "title": "owner"
    }
}
```

Paths are relative to the manifest, so the whole directory can be copied to a Pi or moved anywhere without a single path being rewritten. An absolute path is left alone, for a gallery shared between machines.

- `face` is any reasonably lit photograph with one face in it
- `voice` is at least a few seconds of them speaking alone
- `priority` is rank, zero being the top
- `title` is what the interface calls them: "owner", "trusted", anything you like

Either half on its own works: a person with only a photograph is recognised by sight and not by voice. Both are optional too -- omit them and the conventional locations, `faces/<id>.png` and `voices/<id>.mp3`, are used. A gallery with no manifest at all still works, by file stem, with everybody unranked.

**Rank decides, and the title is what you read.** Nothing on screen says "rank 2" any more; it says "trusted", because a number under somebody's chin is the consequence of a decision rather than the decision itself. The rank still does all the deciding, and the panel still says out loud what it decided -- "owner, may command". A manifest that declares no title falls back to spelling the rank out, so an older gallery loses nothing.

**Rank decides two things, and only two.** When several people are in the room, Kerfur's eyes follow the highest ranked one. And an utterance may only act on the world if the voice it came from is rank 0. Everybody is heard, transcribed, identified and shown; when several people speak at once the higher ranked one is answered first. A command from anybody else is reported as refused rather than silently dropped, because a command that was declined and one that was never heard look identical otherwise.

The directory is deliberately not committed: a face and a voice are personal data, and `.gitignore` keeps them out of the history. Enrolment happens at startup and is cached against a hash of each file, so adding somebody re-reads one file rather than all of them. The directory is looked for wherever `Paths/identitiesPath` says, next to the executable, one level above it, and under the working directory.

#### Adding people while Kerfur runs

The *People* section of the control panel enrols somebody without touching the source tree. Stage a face (*Take from camera*, or a picture file) and a voice (*Use last sentence*, or a recording), give a name, a title and a rank, and press *Save*. Saving a name that is already there updates that person: whatever was staged replaces their file, and the rest stays.

People added this way live in `identities/` under the application data directory (`~/.local/share/kidev/Kerfur/identities` on Linux), in a gallery of the same shape as the one above. On the web build that directory is kept in the browser's IndexedDB, so it survives a reload and never leaves the browser. Both galleries are read together, the panel's last: a record there overrides a shipped record with the same id. Both recognisers re-enrol as soon as a person is saved or removed, with no restart.

The panel writes its own `ids.json` and refuses to rewrite one edited by hand into the list or `identities` form. Pictures are re-encoded as PNG at most 1920 pixels on the longer side, which also drops any metadata the original carried. A sentence used as a voice is stored as 16 bit WAV and has to last at least two seconds.

A voice needs about two seconds of continuous speech before Kerfur will put a name to it. Below that the measurement says more about the room than about the speaker, so it says nothing instead.

**Where a sentence appears** is the answer to who said it. When a voice is recognised and that person's face is in shot at the same time, the camera preview draws the sentence over their head, bordered in the colour their face box already carries. The two halves are matched by name, which is sound because one manifest enrols both galleries: a face and a voice belong to one person because it says so, not because two files happen to share a stem. When the speaker is recognised but not in shot, nothing is drawn -- the transcript is in the hearing panel either way, and a caption floating over a stranger, or over nobody, would be a claim rather than an observation.

### Hands and what is in them
For a person the gallery recognises, Kerfur first locates their hands from the pose keypoints -- a hand sits about a third of a forearm past the wrist, along the forearm, and is about eight tenths of one across. How big that box is, though, is not decided by the forearm alone: an arm reaching towards the camera projects to almost no length at all, and a box sized from it would be far too small to hold the hand at the end of it -- which is the nearest hand to the lens, the largest one in the frame, and the one most likely to be holding something. So the shoulders, the trunk, the eyes and the ears are measured too, each scaled by what a hand is worth against it, and the forearm's answer is kept only where the rest of the body agrees it is possible. That estimate is a region to look in, not a measurement, and it is where the hand pipeline starts rather than where it stops.

That region then goes to MediaPipe's two model hand pipeline: a palm detector, and a landmark model that returns twenty-one points. Both are the ONNX exports from OpenCV Zoo, so they run on the `cv::dnn` already linked rather than pulling in MediaPipe's own Bazel build, and both are pinned by sha256 in `assets/models.json` like every other stage. Upstream runs the palm detector across the whole frame; Kerfur gives it the region the skeleton already found, which is both cheaper and the only way to keep the identity rule below honest.

**Left and right are the subject's own.** The pose model labels the arms of the person it can see, and what it can see is a mirror, so the arm it calls left is the one on the subject's right. Kerfur undoes that where the hands are named rather than where they are drawn: a hand reported as `left` is the one that person would raise if asked to raise their left, whichever side of the mirrored preview it appears on. That is the only version of the answer a skill can be written against.

From the twenty-one points comes a *grip*: zero for a flat open hand, one for a closed fist, measured as how far each fingertip sits from the wrist relative to its own knuckle so that it survives distance and rotation. Measured open hands read below 0.07 and a fist saturates at 1.0. A hand that is plainly open is not holding anything, so the COCO object detector is not run on it.

For hands that pass, the object detector is shown a square crop taken around the hand rather than the whole frame. It has one fixed input side, so a mug thirty pixels across in a 960 wide frame is thirteen pixels by the time the network sees it -- enough to notice that something is there and nowhere near enough to say what it is. In a crop the same mug arrives at a hundred and fifty, which is the difference between "an object" and "a cup". An object is then reported as *held* only when it actually overlaps the hand; something lying on the desk in front of somebody is a thing in the room, not a thing in their hand. Grip is the cheap filter and the overlap is the arbiter, because grip can tell a fist from an open palm but cannot tell a fist from a fist around a mug.

A name has to be seen twice in three looks before it is published. One pass's best class over a small crop flickers between the two nearest things it could be -- a cup and a bowl, a phone and a remote -- and the flicker is worse than silence, because this string is what a skill is written against and what Kerfur says out loud.

The same twenty-one points are also counted: how many digits are held out, from zero to five, thumb included, so a hand held up to say three reads as three and a thumbs up reads as one. The count is shown for every measured hand -- `left 3/5` -- and it is the one reading here that never abstains. It is only ever displayed, never acted on, so each digit is decided at a single line instead of the two-band test below, and a hand between shapes still comes back as a number rather than as nothing.

The same twenty-one points are read for a *gesture*: an open palm, a fist, a thumb up or down, a point, a victory, or a thumb and finger ring. Every rule is a ratio between two lengths of the same hand, so the answer does not change with how near the person is or which way the hand is turned, and the set is chosen to survive reflection -- a thumb pointing up still points up in a mirror, which is not true of anything that depends on which hand is which. A hand caught between two shapes is reported as no shape at all rather than as the nearer of the two: the gesture is the sort of thing that gets acted on, and a wrong answer costs more there than no answer.

The camera preview draws the twenty-one points as a hand skeleton, over the body one, with the fingertips and the wrist marked larger than the knuckles. Where the landmark model has measured a hand, its palm base replaces the pose model's wrist joint on the skeleton: the two are the same joint seen by two models, one guessed from a seventeen point body and one measured from a cropped hand, so the forearm ends exactly where the hand begins instead of a knuckle short of it. Both models are optional. Without them the forearm estimate stands on its own exactly as before, and the overlay says so -- a dimmed box with nothing inside it -- rather than drawing a guess as though it were a measurement. Two hands per frame get the landmark pass, chosen by rank and then by nearness -- the same order the eyes follow -- so a room with four recognised people costs what a room with one costs.

None of this runs for a stranger. That is not an optimization dressed up as a rule: a camera that reads what strangers are carrying is a different device from one that notices its owner is holding a mug, and the identity check is the line between them.

### Skills
A skill is a `.qml` file in `skills/`, read at startup, editable without rebuilding anything. `skills/calories.qml` is a worked example of every part:

```qml
import Skills

Skill {
    id: day
    name: "calories"

    Variable { key: "caloriesOfTheDay"; defaultValue: 0 }

    Routine {
        dailyAt: "00:00"
        onTriggered: day.set("caloriesOfTheDay", 0)
    }

    Trigger {
        intent: "when asked about adding calories"
        phrases: ["add {amount} calories", "i ate {amount} calories"]
        onFired: captures => day.add("caloriesOfTheDay", captures.amount)
    }

    Trigger {
        intent: "when asked to set the number of calories per day"
        phrases: ["set my daily calorie limit to {amount}"]
        confirm: "Are you sure you want to set the daily calorie limit to {amount}?"
        onFired: captures => day.set("maxCaloriesPerDay", captures.amount)
    }

    Trigger {
        intent: "when asked about the current calories"
        onFired: day.say("You are at <caloriesOfTheDay> of <maxCaloriesPerDay> calories")
    }

    Watch {
        key: "caloriesOfTheDay"
        onChanged: value => {
            if (value > day.number("maxCaloriesPerDay"))
                day.emitEvent("calorieLimitExceeded", { "total": value });
        }
    }
}
```

- **`intent`** is the request written as a sentence. It identifies the trigger, it is what a language model would be shown, and on its own it is enough to match on.
- **`phrases`** are the exact forms the local matcher recognises, with `{slot}` placeholders. A value is converted to a number whether it was spoken as digits or as words, because Whisper writes "three hundred" as often as it writes "300". People say "hey kerfur could you add three hundred calories please" and the pattern is "add {amount} calories"; the matcher expects that.
- **`confirm`** asks before acting. Nothing is written until the person who was asked says yes, and only their answer counts -- a second voice saying "yes" across the room is just another sentence.
- **`authority`** is the lowest rank allowed to fire a trigger. Zero by default; `Trigger.Anyone` opens it to strangers.
- **`Routine`** runs on the clock whether or not anybody is talking. `dailyAt` is wall clock and re-arms against the clock, so it does not drift and does not skip when the machine was asleep over midnight.
- **`Variable` / `Watch`** declare and react to shared state. Everything lives on `Blackboard`, persisted to `blackboard.json`, and QML binds to it by name:

  ```qml
  readonly property real calories: Blackboard.values.caloriesOfTheDay
  onCaloriesChanged: ...
  ```
- **`emitEvent`** is the only way out of the skill sandbox. A skill cannot call a house automation system directly; it raises a named event on `Skills.eventRaised` and something else acts on it, so that no skill file can be the reason the robot will not start.

Skills are matched by `LanguageBackend`, which has one implementation today -- phrases and descriptions, matched locally, offline, in a microsecond. The seam is asynchronous for one reason: when there is a language model, it becomes a subclass that posts the same request and emits the same resolution, and every skill already written keeps working unchanged. The panel says which backend is in use.

The `Reload` button in the control panel re-reads the directory. Edit a skill, press it, no rebuild and no restart.

Two build knobs select what is compiled:
- `KERFUR_PLATFORM` is `pi`, `desktop`, `wasm` or `auto`
- `KERFUR_PERCEPTION_BACKEND` is `cpu`, `hailo`, `none` or `auto`

The CPU backend runs on desktop and in the browser through OpenCV DNN. Which models it uses, and why those and not others, is in `docs/superpowers/specs/2026-08-19-cpu-perception-backends.md`.  
