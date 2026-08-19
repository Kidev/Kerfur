# Kerfur P1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Core and Behaviour modules that own all perception state and
gaze control, invert the module dependencies so Display and Tools no longer import
Vision and Hearing, split the build by platform, and delete the old CPU inference
stack.

**Architecture:** Producers push immutable `PerceptionFrame` values through a
bounded queue into a single `WorldModel` QML singleton. Consumers read that
singleton. A `BehaviourEngine` reads `WorldModel` and drives the face, taking over
the gaze control loop that currently runs as arithmetic inside QML. Nothing
imports a producer, so a platform without inference hardware simply does not build
those modules.

**Tech Stack:** Qt 6.11.1 (Core, Gui, Quick, Qml, Quick Controls 2, Multimedia,
Test), C++20, CMake 3.22+, vcpkg, Qt Test with CTest.

**Spec:** `docs/superpowers/specs/2026-08-19-kerfur-perception-rework-design.md`

## Global Constraints

Every task's requirements implicitly include this section.

- **Never run `make`.** The user builds and reports results. Every task ends by
  handing the user an exact command to run. Do not run `make`, `cmake --build`,
  or `ctest` yourself.
- Qt version is 6.11.1 at `/opt/Qt/6.11.1/gcc_64`. C++ standard is 20.
- Brace initialization everywhere, written with no space before the brace:
  `int a{0};`, `QString name{QStringLiteral("kerfur")};`.
- Every control statement is braced, including single statement bodies.
- Never a C-style cast and never the constructor form `T(x)` on a single
  expression. Every conversion is an explicit `static_cast`, `qobject_cast`, or at
  a C API boundary `reinterpret_cast` or `const_cast`.
- No exceptions and no RTTI in Kerfur's own code.
- Every `QObject` subclass carries `Q_OBJECT`. Reimplemented virtuals use
  `override` and drop `virtual`.
- C++ files are named in lower case after the single primary class they contain
  (`worldmodel.h`, `worldmodel.cpp`). QML files keep the PascalCase type name.
- Includes run paired header, then module, then Qt, then standard library, each
  group separated by a blank line.
- Every source file opens with, in the file's comment syntax:
  `SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux` and
  `SPDX-License-Identifier: AGPL-3.0-or-later`.
- Comments explain why, and what breaks otherwise. They never restate the
  signature below them.
- Plain ASCII everywhere, including commit messages. No emoji.
- Commit messages are a single capitalized line, no body, no trailers. Work stays
  on `main`. Never push.
- Compiler flags are already `-Wall -Werror -Wpedantic`. Warnings are build
  failures, so unused parameters must be named out or marked.

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `modules/Core/CMakeLists.txt` | the Core QML module target |
| `modules/Core/src/queuestatssource.h/.cpp` | `QueueStats` and the interface ResourceMonitor collects through |
| `modules/Core/src/boundedqueue.h` | the one fixed capacity, drop oldest queue used everywhere |
| `modules/Core/src/perceptiontypes.h/.cpp` | `Keypoint`, `Keypoint2D`, `TrackedPerson`, `PerceptionFrame` |
| `modules/Core/src/peoplemodel.h/.cpp` | `QAbstractListModel` over tracked people, diffed by track id |
| `modules/Core/src/worldmodel.h/.cpp` | the perception singleton every consumer reads |
| `modules/Core/src/perceptionpipe.h/.cpp` | thread crossing: bounded queue plus queued drain |
| `modules/Core/src/resourcemonitor.h/.cpp` | resident set size, descriptors, threads, queue occupancy |
| `modules/Behaviour/CMakeLists.txt` | the Behaviour QML module target |
| `modules/Behaviour/src/gazecontroller.h/.cpp` | the gaze control loop, pure math, no Qt objects |
| `modules/Behaviour/src/behaviourengine.h/.cpp` | the singleton the face binds to |
| `modules/Vision/src/visionproducer.h/.cpp` | the `pi` only producer shell that P2 fills in |
| `modules/Hearing/src/hearingproducer.h/.cpp` | the `pi` only producer shell that P3 fills in |
| `cmake/platform.cmake` | resolves and validates `KERFUR_PLATFORM` |
| `tests/CMakeLists.txt` | the CTest tree |
| `tests/tst_boundedqueue.cpp` | queue capacity, eviction, statistics, thread safety |
| `tests/tst_perceptiontypes.cpp` | head centre derivation and value semantics |
| `tests/tst_peoplemodel.cpp` | the track id diff |
| `tests/tst_worldmodel.cpp` | aggregation and nearest person selection |
| `tests/tst_perceptionpipe.cpp` | cross thread delivery and drop oldest under load |
| `tests/tst_resourcemonitor.cpp` | sampling and queue reporting |
| `tests/tst_gazecontroller.cpp` | convergence, range scaling, centre return |
| `deploy/kerfur.service` | the systemd unit with restart and watchdog |
| `tools/soak.sh` | the soak harness entry point |

**Modified:**

| Path | Change |
|---|---|
| `.clang-format` | `SpaceBeforeCpp11BracedList: false` |
| `.env` | `QT_ROOT_DIR` to the installed 6.11.1 |
| `CMakeLists.txt` | conditional modules, Qt Test, CTest, platform include |
| `modules/CMakeLists.txt` | conditional `add_subdirectory` |
| `modules/Display/CMakeLists.txt` | depend on Core and Behaviour, not Vision |
| `modules/Tools/CMakeLists.txt` | depend on Core and Behaviour, not Vision |
| `modules/Display/qml/Face.qml` | bind to `WorldModel` and `BehaviourEngine` |
| `modules/Display/qml/LedScreen.qml` | guard the null source, drop the redundant layer |
| `modules/Tools/qml/ControlPanel.qml` | drop the `FaceTracker` requirement and inline recognizer |
| `qml/Main.qml` | stop threading `faceTracker` through |
| `qml/KSettings.qml` | keep only settings that still have an owner |
| `Makefile` | `KERFUR_PLATFORM`, `test` and `soak` targets |
| `vcpkg.json` | drop `whisper-cpp` and `ggml` |

**Deleted:** `modules/Vision/src/AIFaceDetector.{hpp,cpp}`,
`modules/Vision/src/FaceTracker.{hpp,cpp}`,
`modules/Vision/qml/CameraFeedDisplay.qml`,
`modules/Hearing/src/WhisperEngine.{hpp,cpp}`,
`modules/Hearing/src/SpeechRecognizer.{hpp,cpp}`,
`modules/Hearing/qml/SpeechDisplay.qml`.

---

### Task 1: Formatter and toolchain corrections

The formatter currently emits `int i {0}`, which fights the required style on
every file the rest of this plan touches. Fix it before writing any code.

**Files:**
- Modify: `.clang-format:114`
- Modify: `.env:1`

**Interfaces:**
- Consumes: nothing.
- Produces: a tree that `clang-format` leaves alone, so later tasks can format
  freely without unrelated diffs.

- [ ] **Step 1: Flip the brace spacing rule**

In `.clang-format`, change line 114 from `SpaceBeforeCpp11BracedList: true` to:

```yaml
SpaceBeforeCpp11BracedList: false
```

- [ ] **Step 2: Point .env at the installed Qt**

`.env` pins 6.10.0 but `/opt/Qt/6.11.1/gcc_64` is what exists. Replace the first
line so it reads:

```
QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64
VERSION_TAG="0.0.1"
```

- [ ] **Step 3: Reformat the existing C++ so the rule is applied uniformly**

Run:

```bash
clang-format -i modules/Vision/src/*.cpp modules/Vision/src/*.hpp \
               modules/Hearing/src/*.cpp modules/Hearing/src/*.hpp \
               src/main.cpp
```

- [ ] **Step 4: Verify the formatter is now a fixed point**

Run:

```bash
clang-format --dry-run --Werror modules/Vision/src/*.cpp modules/Vision/src/*.hpp \
                                modules/Hearing/src/*.cpp modules/Hearing/src/*.hpp \
                                src/main.cpp && echo CLEAN
```

Expected: `CLEAN`, no diagnostics.

- [ ] **Step 5: Confirm the style actually changed**

Run:

```bash
grep -n "int i{0}" modules/Vision/src/AIFaceDetector.cpp
```

Expected: a match. Before this task the same line read `int i {0}`.

- [ ] **Step 6: Ask the user to build**

Tell the user: "Task 1 changes only formatting and the Qt path. Please run
`make QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64` and report." Wait for the result before
committing.

- [ ] **Step 7: Commit**

```bash
git add .clang-format .env modules/Vision/src modules/Hearing/src src/main.cpp
git commit -m "Format braces without a space and point the build at the installed Qt"
```

---

### Task 2: Test harness

Nothing in this repository is tested today. Every later task in this plan is test
first, so the harness has to exist first and has to be proven by a test that
actually runs.

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/tst_harness.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces: a `tests/` tree where a file named `tst_<name>.cpp` becomes a CTest
  case automatically through `kerfur_add_test(<name>)`, and a `make test` target
  that configures, builds and runs them.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_harness.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>

/// Proves the CTest wiring reaches a real test binary. Everything else in
/// tests/ assumes this works, so when a whole suite goes quiet this is the
/// case that says whether the harness or the code is at fault.
class TestHarness : public QObject
{
    Q_OBJECT

private slots:
    void runsAtAll();
};

void TestHarness::runsAtAll()
{
    QCOMPARE(1 + 1, 2);
}

QTEST_APPLESS_MAIN(TestHarness)
#include "tst_harness.moc"
```

- [ ] **Step 2: Write the test CMake**

Create `tests/CMakeLists.txt`:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# Turns tests/tst_<name>.cpp into a CTest case linked against the given
# libraries. Kept as a function so adding a suite is one line and nobody
# reinvents the target boilerplate per file.
function (kerfur_add_test NAME)
    qt_add_executable(tst_${NAME} tst_${NAME}.cpp)
    target_link_libraries(tst_${NAME} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${NAME} COMMAND tst_${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 60)
endfunction ()

kerfur_add_test(harness)
```

- [ ] **Step 3: Hook the tree into the top level build**

In `CMakeLists.txt`, add `Test` to the `find_package(Qt6 ...)` component list, and
immediately after the `add_subdirectory(modules)` line add:

```cmake
include(CTest)
if (BUILD_TESTING AND NOT EMSCRIPTEN)
    add_subdirectory(tests)
endif ()
```

The WASM configuration is excluded because a browser target has no test runner
here, and `BUILD_TESTING` lets a release build skip the tree entirely.

- [ ] **Step 4: Add the make target**

In `Makefile`, after the `desktop-fast: desktop-build` line, add:

```make
test: deskop-configure
	cmake --build $(BUILD_DIR) --config $(BUILD_MODE)
	cd $(BUILD_DIR) && ctest --output-on-failure
```

- [ ] **Step 5: Ask the user to run the suite**

Tell the user: "Task 2 adds the test harness. Please run `make test` and report."

Expected: the `harness` case passes, `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 6: Commit**

```bash
git add tests CMakeLists.txt Makefile
git commit -m "Add a CTest harness and a make target that runs it"
```

---

### Task 3: BoundedQueue

The queue is the single mechanism that keeps the process from growing, so it is
the first thing built and the most heavily tested.

**Files:**
- Create: `modules/Core/CMakeLists.txt`
- Create: `modules/Core/src/queuestatssource.h`
- Create: `modules/Core/src/queuestatssource.cpp`
- Create: `modules/Core/src/boundedqueue.h`
- Create: `tests/tst_boundedqueue.cpp`
- Modify: `modules/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `kerfur_add_test(<name>)` from Task 2.
- Produces:
  - `struct Kerfur::QueueStats { QString name; qsizetype size; qsizetype capacity; qsizetype highWaterMark; quint64 dropped; }`
  - `class Kerfur::QueueStatsSource` with `virtual QueueStats queueStats() const = 0`
  - `template<typename T> class Kerfur::BoundedQueue final : public QueueStatsSource`
    with `BoundedQueue(qsizetype capacity, QString name)`, `bool push(T value)`,
    `std::optional<T> pop()`, `void clear()`, `QueueStats queueStats() const override`
  - CMake target `Core`

- [ ] **Step 1: Write the failing test**

Create `tests/tst_boundedqueue.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "boundedqueue.h"

#include <QTest>
#include <QThread>

#include <optional>

using Kerfur::BoundedQueue;
using Kerfur::QueueStats;

class TestBoundedQueue : public QObject
{
    Q_OBJECT

private slots:
    void popsInOrder();
    void popsNothingWhenEmpty();
    void evictsTheOldestWhenFull();
    void reportsHighWaterMarkAndDrops();
    void clampsCapacityToAtLeastOne();
    void survivesConcurrentProducers();
};

void TestBoundedQueue::popsInOrder()
{
    BoundedQueue<int> queue{4, QStringLiteral("order")};
    QVERIFY(!queue.push(1));
    QVERIFY(!queue.push(2));
    QCOMPARE(queue.pop().value(), 1);
    QCOMPARE(queue.pop().value(), 2);
}

void TestBoundedQueue::popsNothingWhenEmpty()
{
    BoundedQueue<int> queue{4, QStringLiteral("empty")};
    QVERIFY(!queue.pop().has_value());
}

void TestBoundedQueue::evictsTheOldestWhenFull()
{
    BoundedQueue<int> queue{2, QStringLiteral("evict")};
    QVERIFY(!queue.push(1));
    QVERIFY(!queue.push(2));
    QVERIFY(queue.push(3));

    // 1 is gone rather than 3 being refused: the newest view of the world is
    // the one worth keeping.
    QCOMPARE(queue.pop().value(), 2);
    QCOMPARE(queue.pop().value(), 3);
    QVERIFY(!queue.pop().has_value());
}

void TestBoundedQueue::reportsHighWaterMarkAndDrops()
{
    BoundedQueue<int> queue{2, QStringLiteral("stats")};
    queue.push(1);
    queue.push(2);
    queue.push(3);
    queue.push(4);

    const QueueStats stats{queue.queueStats()};
    QCOMPARE(stats.name, QStringLiteral("stats"));
    QCOMPARE(stats.size, 2);
    QCOMPARE(stats.capacity, 2);
    QCOMPARE(stats.highWaterMark, 2);
    QCOMPARE(stats.dropped, 2U);
}

void TestBoundedQueue::clampsCapacityToAtLeastOne()
{
    BoundedQueue<int> queue{0, QStringLiteral("zero")};
    queue.push(7);
    QCOMPARE(queue.queueStats().capacity, 1);
    QCOMPARE(queue.pop().value(), 7);
}

void TestBoundedQueue::survivesConcurrentProducers()
{
    BoundedQueue<int> queue{8, QStringLiteral("threads")};

    QThread *producer{QThread::create([&queue]() {
        for (int index{0}; index < 5000; ++index) {
            queue.push(index);
        }
    })};
    producer->start();

    for (int index{0}; index < 5000; ++index) {
        queue.push(index);
    }
    QVERIFY(producer->wait(30000));
    delete producer;

    // The only invariant that matters under contention: it never exceeded its
    // capacity, which is what "cannot grow" means.
    QVERIFY(queue.queueStats().size <= 8);
    QVERIFY(queue.queueStats().highWaterMark <= 8);
}

QTEST_APPLESS_MAIN(TestBoundedQueue)
#include "tst_boundedqueue.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `boundedqueue.h: No such file or directory`.

- [ ] **Step 3: Write the statistics interface**

Create `modules/Core/src/queuestatssource.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_QUEUESTATSSOURCE_H
#define KERFUR_QUEUESTATSSOURCE_H

#include <QString>

namespace Kerfur {

/// A snapshot of one queue's occupancy.
///
/// `highWaterMark` and `dropped` are the two numbers worth watching: a queue
/// pinned at its capacity with a climbing drop count means a consumer stopped
/// consuming, which is the shape the previous build's failure would have had if
/// anything had been measuring it.
struct QueueStats
{
    QString name;
    qsizetype size{0};
    qsizetype capacity{0};
    qsizetype highWaterMark{0};
    quint64 dropped{0};
};

/// Implemented by anything ResourceMonitor reports occupancy for.
///
/// This exists so the monitor can hold a list of queues without knowing any of
/// their element types, and so BoundedQueue does not have to be a QObject:
/// queues are created on worker threads and shared across them, which is not
/// what QObject affinity is for.
class QueueStatsSource
{
public:
    QueueStatsSource() = default;
    virtual ~QueueStatsSource();

    QueueStatsSource(const QueueStatsSource &) = delete;
    QueueStatsSource &operator=(const QueueStatsSource &) = delete;

    virtual QueueStats queueStats() const = 0;
};

} // namespace Kerfur

#endif // KERFUR_QUEUESTATSSOURCE_H
```

Create `modules/Core/src/queuestatssource.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "queuestatssource.h"

namespace Kerfur {

// Out of line so the vtable is emitted once rather than in every translation
// unit that instantiates a BoundedQueue.
QueueStatsSource::~QueueStatsSource() = default;

} // namespace Kerfur
```

- [ ] **Step 4: Write the queue**

Create `modules/Core/src/boundedqueue.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_BOUNDEDQUEUE_H
#define KERFUR_BOUNDEDQUEUE_H

#include "queuestatssource.h"

#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include <deque>
#include <optional>
#include <utility>

namespace Kerfur {

/// A fixed capacity queue that drops its oldest element instead of growing.
///
/// Every producer and consumer pair in Kerfur uses one of these. The process has
/// to survive weeks of uptime, and the failure this rework exists to fix was a
/// buffer that grew whenever a producer outran its consumer, so nothing here is
/// allowed to grow with input rate.
///
/// Dropping the oldest rather than refusing the newest is deliberate. A
/// perception pipeline that cannot keep up should show the most recent view of
/// the world, not the head of a stale backlog.
///
/// Safe to use from several threads at once.
template<typename T>
class BoundedQueue final : public QueueStatsSource
{
public:
    /// A capacity below one is clamped to one rather than rejected, because a
    /// misconfigured capacity should degrade to a one deep queue, not to a
    /// queue that silently discards everything.
    BoundedQueue(qsizetype capacity, QString name)
        : m_name{std::move(name)}
        , m_capacity{capacity > 0 ? capacity : 1}
    {
    }

    ~BoundedQueue() override = default;

    /// Appends `value`, evicting the oldest element when already full. Returns
    /// true when something was evicted, so a caller that cares can log it.
    bool push(T value)
    {
        const QMutexLocker locker{&m_mutex};

        bool evicted{false};
        if (static_cast<qsizetype>(m_items.size()) >= m_capacity) {
            m_items.pop_front();
            ++m_dropped;
            evicted = true;
        }
        m_items.push_back(std::move(value));

        const qsizetype occupancy{static_cast<qsizetype>(m_items.size())};
        if (occupancy > m_highWaterMark) {
            m_highWaterMark = occupancy;
        }
        return evicted;
    }

    /// Removes and returns the oldest element, or nothing when empty.
    std::optional<T> pop()
    {
        const QMutexLocker locker{&m_mutex};

        if (m_items.empty()) {
            return std::nullopt;
        }
        std::optional<T> value{std::move(m_items.front())};
        m_items.pop_front();
        return value;
    }

    void clear()
    {
        const QMutexLocker locker{&m_mutex};
        m_items.clear();
    }

    QueueStats queueStats() const override
    {
        const QMutexLocker locker{&m_mutex};
        return QueueStats{m_name,
                          static_cast<qsizetype>(m_items.size()),
                          m_capacity,
                          m_highWaterMark,
                          m_dropped};
    }

private:
    mutable QMutex m_mutex;
    std::deque<T> m_items;
    QString m_name;
    qsizetype m_capacity;
    qsizetype m_highWaterMark{0};
    quint64 m_dropped{0};
};

} // namespace Kerfur

#endif // KERFUR_BOUNDEDQUEUE_H
```

- [ ] **Step 5: Create the Core module target**

Create `modules/Core/CMakeLists.txt`:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(CORE_MODULE Core)

qt_add_library(${CORE_MODULE})

qt_add_qml_module(
    ${CORE_MODULE}
    URI "Core"
    VERSION 1.0
    SOURCES src/queuestatssource.cpp
            src/queuestatssource.h
            src/boundedqueue.h
)

target_link_libraries(${CORE_MODULE} PUBLIC Qt6::Core)

# PUBLIC so that consumers and the test tree can include these headers by their
# bare name, which is how every file in this module refers to its neighbours.
target_include_directories(${CORE_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

In `modules/CMakeLists.txt`, add `add_subdirectory(Core)` as the first line so
the target exists before anything that links it.

- [ ] **Step 6: Register the suite**

In `tests/CMakeLists.txt`, add after `kerfur_add_test(harness)`:

```cmake
kerfur_add_test(boundedqueue Core)
```

- [ ] **Step 7: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 8: Commit**

```bash
git add modules/Core modules/CMakeLists.txt tests
git commit -m "Add the bounded queue every producer and consumer pair will use"
```

---

### Task 4: Perception value types

The immutable payload that crosses the thread boundary, plus the one rule for
deriving where a person's head is that both the behaviour layer and P2's
detectors must agree on.

**Files:**
- Create: `modules/Core/src/perceptiontypes.h`
- Create: `modules/Core/src/perceptiontypes.cpp`
- Create: `tests/tst_perceptiontypes.cpp`
- Modify: `modules/Core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `enum class Kerfur::Keypoint : int` with the seventeen COCO names, and
    `inline constexpr int Kerfur::keypointCount{17}`
  - `struct Kerfur::Keypoint2D { QPointF position; float confidence; }`
  - `class Kerfur::TrackedPerson` (Q_GADGET) with `trackId`, `boundingBox`,
    `headCenter`, `proximity`, `identity`, `identityConfidence`, `emotion`,
    `emotionConfidence`, `heldObjects`, `keypoints`, and matching setters
  - `class Kerfur::PerceptionFrame` (Q_GADGET) with `timestampMs`, `sequence`,
    `orientationDegrees`, `people`
  - `QPointF Kerfur::headCenterFromKeypoints(const QList<Keypoint2D> &keypoints, float minimumConfidence)`

- [ ] **Step 1: Write the failing test**

Create `tests/tst_perceptiontypes.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"

#include <QTest>

using Kerfur::Keypoint;
using Kerfur::Keypoint2D;
using Kerfur::PerceptionFrame;
using Kerfur::TrackedPerson;

class TestPerceptionTypes : public QObject
{
    Q_OBJECT

private slots:
    void headCentreAveragesConfidentFacialKeypoints();
    void headCentreIgnoresUnconfidentKeypoints();
    void headCentreFallsBackToTheBoxTopWhenNothingIsConfident();
    void trackedPersonRoundTripsItsFields();
    void perceptionFrameCarriesItsPeople();
};

static QList<Keypoint2D> makeKeypoints(float confidence)
{
    QList<Keypoint2D> keypoints;
    keypoints.reserve(Kerfur::keypointCount);
    for (int index{0}; index < Kerfur::keypointCount; ++index) {
        keypoints.append(Keypoint2D{QPointF{0.9, 0.9}, 0.0F});
    }
    keypoints[static_cast<int>(Keypoint::Nose)] = Keypoint2D{QPointF{0.50, 0.20}, confidence};
    keypoints[static_cast<int>(Keypoint::LeftEye)] = Keypoint2D{QPointF{0.46, 0.18}, confidence};
    keypoints[static_cast<int>(Keypoint::RightEye)] = Keypoint2D{QPointF{0.54, 0.18}, confidence};
    return keypoints;
}

void TestPerceptionTypes::headCentreAveragesConfidentFacialKeypoints()
{
    const QPointF centre{Kerfur::headCenterFromKeypoints(makeKeypoints(0.9F), 0.5F)};
    QVERIFY(qAbs(centre.x() - 0.50) < 0.0001);
    QVERIFY(qAbs(centre.y() - (0.20 + 0.18 + 0.18) / 3.0) < 0.0001);
}

void TestPerceptionTypes::headCentreIgnoresUnconfidentKeypoints()
{
    QList<Keypoint2D> keypoints{makeKeypoints(0.9F)};

    // Drag one eye far off and mark it unbelievable. A confidence filter that
    // does not work would pull the centre with it.
    keypoints[static_cast<int>(Keypoint::LeftEye)] = Keypoint2D{QPointF{0.05, 0.95}, 0.1F};

    const QPointF centre{Kerfur::headCenterFromKeypoints(keypoints, 0.5F)};
    QVERIFY(qAbs(centre.x() - (0.50 + 0.54) / 2.0) < 0.0001);
}

void TestPerceptionTypes::headCentreFallsBackToTheBoxTopWhenNothingIsConfident()
{
    // Nothing believable at all is a real case: a person facing away has no
    // detectable face. Returning a null point lets the caller decide, rather
    // than returning an average of noise that looks like a real answer.
    const QPointF centre{Kerfur::headCenterFromKeypoints(makeKeypoints(0.1F), 0.5F)};
    QVERIFY(centre.isNull());
}

void TestPerceptionTypes::trackedPersonRoundTripsItsFields()
{
    TrackedPerson person;
    person.setTrackId(7);
    person.setBoundingBox(QRectF{0.1, 0.2, 0.3, 0.4});
    person.setHeadCenter(QPointF{0.25, 0.25});
    person.setProximity(0.4);
    person.setIdentity(QStringLiteral("kidev"), 0.82);
    person.setEmotion(QStringLiteral("happy"), 0.61);
    person.setHeldObjects({QStringLiteral("cup")});
    person.setKeypoints(makeKeypoints(0.9F));

    QCOMPARE(person.trackId(), 7);
    QCOMPARE(person.boundingBox(), QRectF(0.1, 0.2, 0.3, 0.4));
    QCOMPARE(person.headCenter(), QPointF(0.25, 0.25));
    QCOMPARE(person.proximity(), 0.4);
    QCOMPARE(person.identity(), QStringLiteral("kidev"));
    QCOMPARE(person.identityConfidence(), 0.82);
    QCOMPARE(person.emotion(), QStringLiteral("happy"));
    QCOMPARE(person.emotionConfidence(), 0.61);
    QCOMPARE(person.heldObjects(), QStringList{QStringLiteral("cup")});
    QCOMPARE(person.keypoints().size(), Kerfur::keypointCount);
}

void TestPerceptionTypes::perceptionFrameCarriesItsPeople()
{
    TrackedPerson person;
    person.setTrackId(3);

    PerceptionFrame frame;
    frame.setTimestampMs(1234);
    frame.setSequence(9);
    frame.setOrientationDegrees(180);
    frame.setPeople({person});

    QCOMPARE(frame.timestampMs(), 1234);
    QCOMPARE(frame.sequence(), 9U);
    QCOMPARE(frame.orientationDegrees(), 180);
    QCOMPARE(frame.people().size(), 1);
    QCOMPARE(frame.people().first().trackId(), 3);
}

QTEST_APPLESS_MAIN(TestPerceptionTypes)
#include "tst_perceptiontypes.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `perceptiontypes.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Core/src/perceptiontypes.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PERCEPTIONTYPES_H
#define KERFUR_PERCEPTIONTYPES_H

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// COCO keypoint indices, in the order the pose models emit them.
///
/// The numeric values are load bearing: P2 indexes model output directly with
/// them, and the upside down detection compares Nose against the hips, so this
/// order must not be rearranged for readability.
enum class Keypoint : int {
    Nose = 0,
    LeftEye,
    RightEye,
    LeftEar,
    RightEar,
    LeftShoulder,
    RightShoulder,
    LeftElbow,
    RightElbow,
    LeftWrist,
    RightWrist,
    LeftHip,
    RightHip,
    LeftKnee,
    RightKnee,
    LeftAnkle,
    RightAnkle
};

inline constexpr int keypointCount{17};

/// One keypoint: a position normalized to the frame, and how much the model
/// believes it.
///
/// The confidence travels with the position rather than being filtered out
/// upstream because different consumers need different floors. Gaze wants only
/// keypoints it can trust; the orientation check would rather have weak
/// evidence than none.
struct Keypoint2D
{
    QPointF position;
    float confidence{0.0F};
};

/// Registers the perception types for queued connections.
///
/// Q_GADGET already makes them known to QMetaType, so QVariant and the property
/// system need nothing. What a queued connection additionally needs is the name
/// lookup, and the documented contract is that it happens before the first such
/// connection is made. PerceptionPipe's constructor calls this, which is early
/// enough because the pipe is what every queued perception connection goes
/// through. Safe to call repeatedly.
void ensurePerceptionMetaTypesRegistered();

/// The mean of the confident facial keypoints, or a null point when none of
/// them clear `minimumConfidence`.
///
/// Returning null rather than a fallback is deliberate. A person facing away has
/// no detectable face, and an average of noise looks exactly like a real answer
/// to a caller that cannot tell the difference.
QPointF headCenterFromKeypoints(const QList<Keypoint2D> &keypoints, float minimumConfidence);

/// One person the tracker is following, as everything downstream sees them.
///
/// All geometry is normalized to the frame so that a consumer never has to know
/// the capture resolution, which differs between the two camera streams and
/// changes with orientation.
class TrackedPerson
{
    Q_GADGET
    QML_VALUE_TYPE(trackedPerson)

    Q_PROPERTY(int trackId READ trackId)
    Q_PROPERTY(QRectF boundingBox READ boundingBox)
    Q_PROPERTY(QPointF headCenter READ headCenter)
    Q_PROPERTY(qreal proximity READ proximity)
    Q_PROPERTY(QString identity READ identity)
    Q_PROPERTY(qreal identityConfidence READ identityConfidence)
    Q_PROPERTY(QString emotion READ emotion)
    Q_PROPERTY(qreal emotionConfidence READ emotionConfidence)
    Q_PROPERTY(QStringList heldObjects READ heldObjects)
    Q_PROPERTY(bool known READ isKnown)

public:
    int trackId() const { return m_trackId; }
    QRectF boundingBox() const { return m_boundingBox; }
    QPointF headCenter() const { return m_headCenter; }

    /// How much of the frame height this person occupies. This is what "close"
    /// means everywhere in Kerfur: it needs no camera calibration and it is
    /// monotonic in real distance for a fixed lens.
    qreal proximity() const { return m_proximity; }

    QString identity() const { return m_identity; }
    qreal identityConfidence() const { return m_identityConfidence; }
    QString emotion() const { return m_emotion; }
    qreal emotionConfidence() const { return m_emotionConfidence; }
    QStringList heldObjects() const { return m_heldObjects; }
    QList<Keypoint2D> keypoints() const { return m_keypoints; }

    bool isKnown() const { return !m_identity.isEmpty(); }

    void setTrackId(int trackId) { m_trackId = trackId; }
    void setBoundingBox(const QRectF &boundingBox) { m_boundingBox = boundingBox; }
    void setHeadCenter(const QPointF &headCenter) { m_headCenter = headCenter; }
    void setProximity(qreal proximity) { m_proximity = proximity; }
    void setIdentity(const QString &identity, qreal confidence);
    void setEmotion(const QString &emotion, qreal confidence);
    void setHeldObjects(const QStringList &heldObjects) { m_heldObjects = heldObjects; }
    void setKeypoints(const QList<Keypoint2D> &keypoints) { m_keypoints = keypoints; }

private:
    QRectF m_boundingBox;
    QPointF m_headCenter;
    QString m_identity;
    QString m_emotion;
    QStringList m_heldObjects;
    QList<Keypoint2D> m_keypoints;
    qreal m_proximity{0.0};
    qreal m_identityConfidence{0.0};
    qreal m_emotionConfidence{0.0};
    int m_trackId{-1};
};

/// Everything one processed camera frame concluded, as one value.
///
/// Producers hand exactly one of these across the thread boundary per frame.
/// The previous build emitted a separate signal per changed scalar, which meant
/// consumers saw torn state and the encoder property was recomputed several
/// times per frame.
class PerceptionFrame
{
    Q_GADGET
    QML_VALUE_TYPE(perceptionFrame)

    Q_PROPERTY(qint64 timestampMs READ timestampMs)
    Q_PROPERTY(int orientationDegrees READ orientationDegrees)

public:
    qint64 timestampMs() const { return m_timestampMs; }
    quint64 sequence() const { return m_sequence; }
    int orientationDegrees() const { return m_orientationDegrees; }
    QList<TrackedPerson> people() const { return m_people; }

    void setTimestampMs(qint64 timestampMs) { m_timestampMs = timestampMs; }
    void setSequence(quint64 sequence) { m_sequence = sequence; }
    void setOrientationDegrees(int degrees) { m_orientationDegrees = degrees; }
    void setPeople(const QList<TrackedPerson> &people) { m_people = people; }

private:
    QList<TrackedPerson> m_people;
    qint64 m_timestampMs{0};
    quint64 m_sequence{0};
    int m_orientationDegrees{0};
};

} // namespace Kerfur

#endif // KERFUR_PERCEPTIONTYPES_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Core/src/perceptiontypes.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"

namespace Kerfur {

namespace {

// Nose and both eyes. Ears are excluded on purpose: they are the keypoints a
// profile view keeps when the eyes are gone, and averaging one visible ear into
// the centre pulls the gaze target off the face rather than onto it.
constexpr Keypoint facialKeypoints[]{Keypoint::Nose, Keypoint::LeftEye, Keypoint::RightEye};

} // namespace

QPointF headCenterFromKeypoints(const QList<Keypoint2D> &keypoints, float minimumConfidence)
{
    if (keypoints.size() < keypointCount) {
        return {};
    }

    QPointF total;
    int counted{0};
    for (const Keypoint keypoint : facialKeypoints) {
        const Keypoint2D &candidate{keypoints.at(static_cast<int>(keypoint))};
        if (candidate.confidence < minimumConfidence) {
            continue;
        }
        total += candidate.position;
        ++counted;
    }

    if (counted == 0) {
        return {};
    }
    return total / counted;
}

void ensurePerceptionMetaTypesRegistered()
{
    // The templated no-argument form. Qt 6 removed the overload that took a
    // type name string, so passing "Kerfur::PerceptionFrame" here would not
    // compile.
    qRegisterMetaType<TrackedPerson>();
    qRegisterMetaType<PerceptionFrame>();
}

void TrackedPerson::setIdentity(const QString &identity, qreal confidence)
{
    m_identity = identity;
    m_identityConfidence = confidence;
}

void TrackedPerson::setEmotion(const QString &emotion, qreal confidence)
{
    m_emotion = emotion;
    m_emotionConfidence = confidence;
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Core/CMakeLists.txt`, add `src/perceptiontypes.cpp` and
`src/perceptiontypes.h` to the `SOURCES` list, and add `Qt6::Qml` to
`target_link_libraries` (the value types are registered with `QML_VALUE_TYPE`).

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(perceptiontypes Core)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 7: Commit**

```bash
git add modules/Core tests
git commit -m "Add the perception value types that cross the thread boundary"
```

---

### Task 5: PeopleModel

A list model over the tracked people, updated by diffing on track id rather than
resetting. A reset per frame destroys and rebuilds every delegate thirty times a
second, which is both the visible stutter and a steady allocation churn.

**Files:**
- Create: `modules/Core/src/peoplemodel.h`
- Create: `modules/Core/src/peoplemodel.cpp`
- Create: `tests/tst_peoplemodel.cpp`
- Modify: `modules/Core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Kerfur::TrackedPerson` from Task 4.
- Produces: `class Kerfur::PeopleModel : public QAbstractListModel` with
  `enum Role { TrackIdRole = Qt::UserRole + 1, BoundingBoxRole, HeadCenterRole, ProximityRole, IdentityRole, IdentityConfidenceRole, EmotionRole, EmotionConfidenceRole, HeldObjectsRole }`,
  `void setPeople(const QList<TrackedPerson> &people)`, `int count() const`,
  `Q_SIGNAL void countChanged()`.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_peoplemodel.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "peoplemodel.h"
#include "perceptiontypes.h"

#include <QSignalSpy>
#include <QTest>

using Kerfur::PeopleModel;
using Kerfur::TrackedPerson;

static TrackedPerson makePerson(int trackId, const QString &emotion)
{
    TrackedPerson person;
    person.setTrackId(trackId);
    person.setProximity(0.1 * trackId);
    person.setEmotion(emotion, 0.9);
    return person;
}

class TestPeopleModel : public QObject
{
    Q_OBJECT

private slots:
    void startsEmpty();
    void exposesTheExpectedRoleNames();
    void insertsNewTracks();
    void updatesAnExistingTrackWithoutReinserting();
    void removesAVanishedTrack();
    void keepsRowsOrderedByTrackId();
};

void TestPeopleModel::startsEmpty()
{
    PeopleModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
}

void TestPeopleModel::exposesTheExpectedRoleNames()
{
    PeopleModel model;
    const QHash<int, QByteArray> names{model.roleNames()};
    QCOMPARE(names.value(PeopleModel::TrackIdRole), QByteArray{"trackId"});
    QCOMPARE(names.value(PeopleModel::IdentityRole), QByteArray{"identity"});
    QCOMPARE(names.value(PeopleModel::EmotionRole), QByteArray{"emotion"});
    QCOMPARE(names.value(PeopleModel::HeldObjectsRole), QByteArray{"heldObjects"});
}

void TestPeopleModel::insertsNewTracks()
{
    PeopleModel model;
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};

    model.setPeople({makePerson(1, QStringLiteral("neutral")),
                     makePerson(2, QStringLiteral("happy"))});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.data(model.index(0), PeopleModel::TrackIdRole).toInt(), 1);
    QCOMPARE(model.data(model.index(1), PeopleModel::EmotionRole).toString(),
             QStringLiteral("happy"));
}

void TestPeopleModel::updatesAnExistingTrackWithoutReinserting()
{
    PeopleModel model;
    model.setPeople({makePerson(1, QStringLiteral("neutral"))});

    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};

    model.setPeople({makePerson(1, QStringLiteral("angry"))});

    // The whole point of diffing: a delegate for track 1 survives the update.
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.data(model.index(0), PeopleModel::EmotionRole).toString(),
             QStringLiteral("angry"));
}

void TestPeopleModel::removesAVanishedTrack()
{
    PeopleModel model;
    model.setPeople({makePerson(1, QStringLiteral("neutral")),
                     makePerson(2, QStringLiteral("happy")),
                     makePerson(3, QStringLiteral("sad"))});

    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    model.setPeople({makePerson(1, QStringLiteral("neutral")),
                     makePerson(3, QStringLiteral("sad"))});

    QCOMPARE(removed.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1), PeopleModel::TrackIdRole).toInt(), 3);
}

void TestPeopleModel::keepsRowsOrderedByTrackId()
{
    PeopleModel model;
    model.setPeople({makePerson(9, QStringLiteral("a")),
                     makePerson(2, QStringLiteral("b")),
                     makePerson(5, QStringLiteral("c"))});

    QCOMPARE(model.data(model.index(0), PeopleModel::TrackIdRole).toInt(), 2);
    QCOMPARE(model.data(model.index(1), PeopleModel::TrackIdRole).toInt(), 5);
    QCOMPARE(model.data(model.index(2), PeopleModel::TrackIdRole).toInt(), 9);
}

QTEST_GUILESS_MAIN(TestPeopleModel)
#include "tst_peoplemodel.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `peoplemodel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Core/src/peoplemodel.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PEOPLEMODEL_H
#define KERFUR_PEOPLEMODEL_H

#include "perceptiontypes.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// The people the tracker is following, as a list model.
///
/// Rows are kept sorted by track id and updated by diffing rather than by
/// resetting. A reset per frame tears down and rebuilds every delegate at the
/// frame rate, which is both visible as stutter and a steady allocation churn in
/// a process that has to run for weeks.
class PeopleModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PeopleModel is owned by WorldModel")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        BoundingBoxRole,
        HeadCenterRole,
        ProximityRole,
        IdentityRole,
        IdentityConfidenceRole,
        EmotionRole,
        EmotionConfidenceRole,
        HeldObjectsRole
    };
    Q_ENUM(Role)

    explicit PeopleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    /// Replaces the contents with `people`, emitting the narrowest set of model
    /// signals that describes the change.
    void setPeople(const QList<TrackedPerson> &people);

signals:
    void countChanged();

private:
    QList<TrackedPerson> m_people;
};

} // namespace Kerfur

#endif // KERFUR_PEOPLEMODEL_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Core/src/peoplemodel.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "peoplemodel.h"

#include <algorithm>

namespace Kerfur {

PeopleModel::PeopleModel(QObject *parent)
    : QAbstractListModel{parent}
{
}

int PeopleModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_people.size());
}

int PeopleModel::count() const
{
    return static_cast<int>(m_people.size());
}

QVariant PeopleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_people.size()) {
        return {};
    }

    const TrackedPerson &person{m_people.at(index.row())};
    switch (role) {
    case TrackIdRole:
        return person.trackId();
    case BoundingBoxRole:
        return person.boundingBox();
    case HeadCenterRole:
        return person.headCenter();
    case ProximityRole:
        return person.proximity();
    case IdentityRole:
        return person.identity();
    case IdentityConfidenceRole:
        return person.identityConfidence();
    case EmotionRole:
        return person.emotion();
    case EmotionConfidenceRole:
        return person.emotionConfidence();
    case HeldObjectsRole:
        return person.heldObjects();
    default:
        return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const
{
    return {{TrackIdRole, QByteArrayLiteral("trackId")},
            {BoundingBoxRole, QByteArrayLiteral("boundingBox")},
            {HeadCenterRole, QByteArrayLiteral("headCenter")},
            {ProximityRole, QByteArrayLiteral("proximity")},
            {IdentityRole, QByteArrayLiteral("identity")},
            {IdentityConfidenceRole, QByteArrayLiteral("identityConfidence")},
            {EmotionRole, QByteArrayLiteral("emotion")},
            {EmotionConfidenceRole, QByteArrayLiteral("emotionConfidence")},
            {HeldObjectsRole, QByteArrayLiteral("heldObjects")}};
}

void PeopleModel::setPeople(const QList<TrackedPerson> &people)
{
    QList<TrackedPerson> incoming{people};
    std::sort(incoming.begin(), incoming.end(), [](const TrackedPerson &left,
                                                   const TrackedPerson &right) {
        return left.trackId() < right.trackId();
    });

    const qsizetype previousCount{m_people.size()};

    // Both lists are sorted by track id, so one walk finds every insertion,
    // removal and in place update without a second pass or a lookup table.
    qsizetype row{0};
    qsizetype next{0};
    while (row < m_people.size() || next < incoming.size()) {
        if (next >= incoming.size()) {
            beginRemoveRows(QModelIndex{},
                            static_cast<int>(row),
                            static_cast<int>(m_people.size() - 1));
            m_people.remove(row, m_people.size() - row);
            endRemoveRows();
            break;
        }

        if (row >= m_people.size()) {
            beginInsertRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.insert(row, incoming.at(next));
            endInsertRows();
            ++row;
            ++next;
            continue;
        }

        const int existingId{m_people.at(row).trackId()};
        const int incomingId{incoming.at(next).trackId()};

        if (existingId == incomingId) {
            m_people[row] = incoming.at(next);
            const QModelIndex changed{index(static_cast<int>(row))};
            emit dataChanged(changed, changed);
            ++row;
            ++next;
        } else if (existingId < incomingId) {
            beginRemoveRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.remove(row);
            endRemoveRows();
        } else {
            beginInsertRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.insert(row, incoming.at(next));
            endInsertRows();
            ++row;
            ++next;
        }
    }

    if (m_people.size() != previousCount) {
        emit countChanged();
    }
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Core/CMakeLists.txt`, add `src/peoplemodel.cpp` and
`src/peoplemodel.h` to `SOURCES`.

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(peoplemodel Core)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 7: Commit**

```bash
git add modules/Core tests
git commit -m "Add a people model that diffs on track id instead of resetting"
```

---

### Task 6: WorldModel

The singleton that owns everything Kerfur believes about the world, and the only
thing consumers are allowed to read.

**Files:**
- Create: `modules/Core/src/worldmodel.h`
- Create: `modules/Core/src/worldmodel.cpp`
- Create: `tests/tst_worldmodel.cpp`
- Modify: `modules/Core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Kerfur::PerceptionFrame`, `Kerfur::TrackedPerson` (Task 4),
  `Kerfur::PeopleModel` (Task 5).
- Produces: `class Kerfur::WorldModel : public QObject` as a QML singleton, with
  `static WorldModel *create(QQmlEngine *, QJSEngine *)`,
  `static WorldModel &instance()`,
  slots `applyPerceptionFrame(const PerceptionFrame &)`, `setHeardText(const QString &)`,
  `setWakeWordActive(bool)`, `setCameraOnline(bool)`, `setMicrophoneOnline(bool)`,
  `setCameraOrientation(int)`, and properties `personPresent`, `personCount`,
  `hasNearestPerson`, `nearestPerson`, `nearestHeadCenter`, `people`, `heardText`,
  `wakeWordActive`, `cameraOnline`, `microphoneOnline`, `cameraOrientation`.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_worldmodel.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"
#include "worldmodel.h"

#include <QSignalSpy>
#include <QTest>

using Kerfur::PerceptionFrame;
using Kerfur::TrackedPerson;
using Kerfur::WorldModel;

static TrackedPerson makePerson(int trackId, qreal proximity, const QPointF &head)
{
    TrackedPerson person;
    person.setTrackId(trackId);
    person.setProximity(proximity);
    person.setHeadCenter(head);
    return person;
}

class TestWorldModel : public QObject
{
    Q_OBJECT

private slots:
    void startsWithNobodyPresent();
    void reportsPresenceAndCount();
    void picksTheNearestPersonByProximity();
    void emitsPerceptionChangedOncePerFrame();
    void clearsTheNearestPersonWhenEverybodyLeaves();
    void onlyNotifiesHeardTextOnAChange();
    void tracksCameraAndMicrophoneAvailability();
};

void TestWorldModel::startsWithNobodyPresent()
{
    WorldModel model;
    QCOMPARE(model.personCount(), 0);
    QVERIFY(!model.isPersonPresent());
    QVERIFY(!model.hasNearestPerson());
    QVERIFY(model.nearestHeadCenter().isNull());
}

void TestWorldModel::reportsPresenceAndCount()
{
    WorldModel model;
    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3}),
                     makePerson(2, 0.5, QPointF{0.6, 0.35})});
    model.applyPerceptionFrame(frame);

    QCOMPARE(model.personCount(), 2);
    QVERIFY(model.isPersonPresent());
    QCOMPARE(model.people()->rowCount(), 2);
}

void TestWorldModel::picksTheNearestPersonByProximity()
{
    WorldModel model;
    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3}),
                     makePerson(2, 0.5, QPointF{0.6, 0.35}),
                     makePerson(3, 0.35, QPointF{0.1, 0.9})});
    model.applyPerceptionFrame(frame);

    QVERIFY(model.hasNearestPerson());
    QCOMPARE(model.nearestPerson().trackId(), 2);
    QCOMPARE(model.nearestHeadCenter(), QPointF(0.6, 0.35));
}

void TestWorldModel::emitsPerceptionChangedOncePerFrame()
{
    WorldModel model;
    QSignalSpy spy{&model, &WorldModel::perceptionChanged};

    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3})});
    model.applyPerceptionFrame(frame);

    // One frame is one notification. The build this replaces emitted five
    // separate property signals per frame, so consumers saw torn state.
    QCOMPARE(spy.count(), 1);
}

void TestWorldModel::clearsTheNearestPersonWhenEverybodyLeaves()
{
    WorldModel model;
    PerceptionFrame occupied;
    occupied.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3})});
    model.applyPerceptionFrame(occupied);
    QVERIFY(model.hasNearestPerson());

    model.applyPerceptionFrame(PerceptionFrame{});

    QVERIFY(!model.hasNearestPerson());
    QVERIFY(!model.isPersonPresent());
    QVERIFY(model.nearestHeadCenter().isNull());
    QCOMPARE(model.people()->rowCount(), 0);
}

void TestWorldModel::onlyNotifiesHeardTextOnAChange()
{
    WorldModel model;
    QSignalSpy spy{&model, &WorldModel::heardTextChanged};

    model.setHeardText(QStringLiteral("hello kerfur"));
    model.setHeardText(QStringLiteral("hello kerfur"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.heardText(), QStringLiteral("hello kerfur"));
}

void TestWorldModel::tracksCameraAndMicrophoneAvailability()
{
    WorldModel model;
    QVERIFY(!model.isCameraOnline());
    QVERIFY(!model.isMicrophoneOnline());

    model.setCameraOnline(true);
    model.setMicrophoneOnline(true);
    model.setCameraOrientation(180);

    QVERIFY(model.isCameraOnline());
    QVERIFY(model.isMicrophoneOnline());
    QCOMPARE(model.cameraOrientation(), 180);
}

QTEST_GUILESS_MAIN(TestWorldModel)
#include "tst_worldmodel.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `worldmodel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Core/src/worldmodel.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_WORLDMODEL_H
#define KERFUR_WORLDMODEL_H

#include "peoplemodel.h"
#include "perceptiontypes.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Everything Kerfur currently believes about the world, in one place.
///
/// Producers push into it and consumers read from it, and nothing else is
/// allowed. The build this replaces let the display layer import the camera and
/// microphone modules and instantiate their objects as QML children, which is
/// why perception state lived in whichever object happened to produce it and why
/// a build without inference hardware was impossible.
///
/// Everything derived from a camera frame notifies through the single
/// `perceptionChanged` signal, so a consumer never observes half of a frame.
class WorldModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool personPresent READ isPersonPresent NOTIFY perceptionChanged)
    Q_PROPERTY(int personCount READ personCount NOTIFY perceptionChanged)
    Q_PROPERTY(bool hasNearestPerson READ hasNearestPerson NOTIFY perceptionChanged)
    Q_PROPERTY(Kerfur::TrackedPerson nearestPerson READ nearestPerson NOTIFY perceptionChanged)
    Q_PROPERTY(QPointF nearestHeadCenter READ nearestHeadCenter NOTIFY perceptionChanged)
    Q_PROPERTY(Kerfur::PeopleModel *people READ people CONSTANT)
    Q_PROPERTY(QString heardText READ heardText NOTIFY heardTextChanged)
    Q_PROPERTY(bool wakeWordActive READ isWakeWordActive NOTIFY wakeWordActiveChanged)
    Q_PROPERTY(bool cameraOnline READ isCameraOnline NOTIFY cameraOnlineChanged)
    Q_PROPERTY(bool microphoneOnline READ isMicrophoneOnline NOTIFY microphoneOnlineChanged)
    Q_PROPERTY(int cameraOrientation READ cameraOrientation NOTIFY cameraOrientationChanged)

public:
    explicit WorldModel(QObject *parent = nullptr);

    /// The one instance, reachable from C++ producers as well as from QML.
    /// The QML singleton machinery alone would give the engine its own instance
    /// that no producer could find.
    static WorldModel &instance();
    static WorldModel *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    bool isPersonPresent() const;
    int personCount() const;
    bool hasNearestPerson() const;
    TrackedPerson nearestPerson() const;
    QPointF nearestHeadCenter() const;
    PeopleModel *people();
    QString heardText() const;
    bool isWakeWordActive() const;
    bool isCameraOnline() const;
    bool isMicrophoneOnline() const;
    int cameraOrientation() const;

public slots:
    /// Producers call this, always on this object's own thread. PerceptionPipe
    /// is what gets them there from a worker thread.
    void applyPerceptionFrame(const Kerfur::PerceptionFrame &frame);

    void setHeardText(const QString &text);
    void setWakeWordActive(bool active);
    void setCameraOnline(bool online);
    void setMicrophoneOnline(bool online);
    void setCameraOrientation(int degrees);

signals:
    void perceptionChanged();
    void heardTextChanged();
    void wakeWordActiveChanged();
    void cameraOnlineChanged();
    void microphoneOnlineChanged();
    void cameraOrientationChanged();

private:
    PeopleModel m_people;
    TrackedPerson m_nearestPerson;
    QString m_heardText;
    int m_personCount{0};
    int m_cameraOrientation{0};
    bool m_hasNearestPerson{false};
    bool m_wakeWordActive{false};
    bool m_cameraOnline{false};
    bool m_microphoneOnline{false};
};

} // namespace Kerfur

#endif // KERFUR_WORLDMODEL_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Core/src/worldmodel.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "worldmodel.h"

#include <QJSEngine>
#include <QQmlEngine>

namespace Kerfur {

WorldModel::WorldModel(QObject *parent)
    : QObject{parent}
{
}

WorldModel &WorldModel::instance()
{
    static WorldModel model;
    return model;
}

WorldModel *WorldModel::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    // C++ owned, because the instance outlives any engine: producers on worker
    // threads hold references to it and a garbage collected singleton would be
    // a use after free waiting for a reload.
    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

bool WorldModel::isPersonPresent() const
{
    return m_personCount > 0;
}

int WorldModel::personCount() const
{
    return m_personCount;
}

bool WorldModel::hasNearestPerson() const
{
    return m_hasNearestPerson;
}

TrackedPerson WorldModel::nearestPerson() const
{
    return m_nearestPerson;
}

QPointF WorldModel::nearestHeadCenter() const
{
    if (!m_hasNearestPerson) {
        return {};
    }
    return m_nearestPerson.headCenter();
}

PeopleModel *WorldModel::people()
{
    return &m_people;
}

QString WorldModel::heardText() const
{
    return m_heardText;
}

bool WorldModel::isWakeWordActive() const
{
    return m_wakeWordActive;
}

bool WorldModel::isCameraOnline() const
{
    return m_cameraOnline;
}

bool WorldModel::isMicrophoneOnline() const
{
    return m_microphoneOnline;
}

int WorldModel::cameraOrientation() const
{
    return m_cameraOrientation;
}

void WorldModel::applyPerceptionFrame(const Kerfur::PerceptionFrame &frame)
{
    const QList<TrackedPerson> people{frame.people()};
    m_people.setPeople(people);
    m_personCount = static_cast<int>(people.size());
    m_cameraOrientation = frame.orientationDegrees();

    // Nearest is the largest share of the frame height. It needs no camera
    // calibration and it is what decides who Kerfur looks at, so a tie is
    // resolved by keeping the first, which is the lower track id and therefore
    // the person who has been here longer.
    m_hasNearestPerson = false;
    qreal bestProximity{-1.0};
    for (const TrackedPerson &person : people) {
        if (person.proximity() > bestProximity) {
            bestProximity = person.proximity();
            m_nearestPerson = person;
            m_hasNearestPerson = true;
        }
    }
    if (!m_hasNearestPerson) {
        m_nearestPerson = TrackedPerson{};
    }

    emit perceptionChanged();
}

void WorldModel::setHeardText(const QString &text)
{
    if (m_heardText == text) {
        return;
    }
    m_heardText = text;
    emit heardTextChanged();
}

void WorldModel::setWakeWordActive(bool active)
{
    if (m_wakeWordActive == active) {
        return;
    }
    m_wakeWordActive = active;
    emit wakeWordActiveChanged();
}

void WorldModel::setCameraOnline(bool online)
{
    if (m_cameraOnline == online) {
        return;
    }
    m_cameraOnline = online;
    emit cameraOnlineChanged();
}

void WorldModel::setMicrophoneOnline(bool online)
{
    if (m_microphoneOnline == online) {
        return;
    }
    m_microphoneOnline = online;
    emit microphoneOnlineChanged();
}

void WorldModel::setCameraOrientation(int degrees)
{
    if (m_cameraOrientation == degrees) {
        return;
    }
    m_cameraOrientation = degrees;
    emit cameraOrientationChanged();
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Core/CMakeLists.txt`, add `src/worldmodel.cpp` and `src/worldmodel.h`
to `SOURCES`.

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(worldmodel Core)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 7: Commit**

```bash
git add modules/Core tests
git commit -m "Add the world model that owns every perception fact"
```

---

### Task 7: PerceptionPipe

The thread crossing. This is where the design's promise that no buffer grows with
input rate is actually enforced against a producer running faster than the GUI
thread drains.

**Files:**
- Create: `modules/Core/src/perceptionpipe.h`
- Create: `modules/Core/src/perceptionpipe.cpp`
- Create: `tests/tst_perceptionpipe.cpp`
- Modify: `modules/Core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Kerfur::BoundedQueue` (Task 3), `Kerfur::PerceptionFrame` (Task 4).
- Produces: `class Kerfur::PerceptionPipe : public QObject` with
  `PerceptionPipe(qsizetype capacity, const QString &name, QObject *parent = nullptr)`,
  `void submit(PerceptionFrame frame)` callable from any thread,
  `QueueStatsSource *statsSource()`, and
  `Q_SIGNAL void frameReady(const Kerfur::PerceptionFrame &frame)` emitted on the
  pipe's own thread.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_perceptionpipe.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptionpipe.h"
#include "perceptiontypes.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

using Kerfur::PerceptionFrame;
using Kerfur::PerceptionPipe;

static PerceptionFrame makeFrame(quint64 sequence)
{
    PerceptionFrame frame;
    frame.setSequence(sequence);
    return frame;
}

class TestPerceptionPipe : public QObject
{
    Q_OBJECT

private slots:
    void deliversASubmittedFrame();
    void dropsTheOldestWhenTheConsumerCannotKeepUp();
    void deliversFromAnotherThreadOnTheOwningThread();
    void reportsItsQueueStatistics();
};

void TestPerceptionPipe::deliversASubmittedFrame()
{
    PerceptionPipe pipe{4, QStringLiteral("deliver")};
    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    pipe.submit(makeFrame(1));
    QVERIFY(spy.wait(2000));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<PerceptionFrame>().sequence(), 1U);
}

void TestPerceptionPipe::dropsTheOldestWhenTheConsumerCannotKeepUp()
{
    PerceptionPipe pipe{2, QStringLiteral("drop")};
    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    // Nothing drains while this loop runs, because the drain is queued and the
    // event loop is not running yet. Five frames into a two deep queue.
    for (quint64 sequence{1}; sequence <= 5; ++sequence) {
        pipe.submit(makeFrame(sequence));
    }
    QCoreApplication::processEvents();

    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).value<PerceptionFrame>().sequence(), 4U);
    QCOMPARE(spy.at(1).at(0).value<PerceptionFrame>().sequence(), 5U);
}

void TestPerceptionPipe::deliversFromAnotherThreadOnTheOwningThread()
{
    PerceptionPipe pipe{8, QStringLiteral("cross")};

    QThread *owningThread{QThread::currentThread()};
    QThread *observed{nullptr};
    connect(&pipe, &PerceptionPipe::frameReady, this, [&observed]() {
        observed = QThread::currentThread();
    });

    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    QThread *producer{QThread::create([&pipe]() { pipe.submit(makeFrame(42)); })};
    producer->start();
    QVERIFY(producer->wait(5000));
    delete producer;

    QVERIFY(spy.wait(2000));
    QCOMPARE(observed, owningThread);
}

void TestPerceptionPipe::reportsItsQueueStatistics()
{
    PerceptionPipe pipe{2, QStringLiteral("stats")};
    for (quint64 sequence{1}; sequence <= 5; ++sequence) {
        pipe.submit(makeFrame(sequence));
    }

    const Kerfur::QueueStats stats{pipe.statsSource()->queueStats()};
    QCOMPARE(stats.name, QStringLiteral("stats"));
    QCOMPARE(stats.capacity, 2);
    QCOMPARE(stats.dropped, 3U);
}

QTEST_GUILESS_MAIN(TestPerceptionPipe)
#include "tst_perceptionpipe.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `perceptionpipe.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Core/src/perceptionpipe.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PERCEPTIONPIPE_H
#define KERFUR_PERCEPTIONPIPE_H

#include "boundedqueue.h"
#include "perceptiontypes.h"

#include <QObject>
#include <QString>

namespace Kerfur {

/// Carries perception frames from a worker thread to the thread that owns this
/// object, dropping the oldest rather than queueing without bound.
///
/// This is the only sanctioned way into WorldModel from a producer. An inference
/// worker that outruns the GUI thread is normal and expected, and what should
/// happen then is that intermediate frames are discarded, not that they pile up
/// until the process is killed.
class PerceptionPipe : public QObject
{
    Q_OBJECT

public:
    PerceptionPipe(qsizetype capacity, const QString &name, QObject *parent = nullptr);

    /// Callable from any thread. Returns immediately; delivery happens later on
    /// the owning thread.
    void submit(PerceptionFrame frame);

    QueueStatsSource *statsSource();

signals:
    void frameReady(const Kerfur::PerceptionFrame &frame);

private:
    void drain();

    BoundedQueue<PerceptionFrame> m_queue;
};

} // namespace Kerfur

#endif // KERFUR_PERCEPTIONPIPE_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Core/src/perceptionpipe.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptionpipe.h"

#include <QMetaObject>

#include <optional>
#include <utility>

namespace Kerfur {

PerceptionPipe::PerceptionPipe(qsizetype capacity, const QString &name, QObject *parent)
    : QObject{parent}
    , m_queue{capacity, name}
{
    ensurePerceptionMetaTypesRegistered();
}

void PerceptionPipe::submit(PerceptionFrame frame)
{
    m_queue.push(std::move(frame));

    // Queued, so drain runs on the thread that owns this object. That is the
    // only thread allowed to touch whatever frameReady is connected to, and it
    // is why submit is safe to call from an inference worker.
    QMetaObject::invokeMethod(this, &PerceptionPipe::drain, Qt::QueuedConnection);
}

QueueStatsSource *PerceptionPipe::statsSource()
{
    return &m_queue;
}

void PerceptionPipe::drain()
{
    // Drains everything rather than one per invocation: submit posts one event
    // per frame, but frames that arrived while an earlier drain was running
    // would otherwise wait for an event that has already been consumed.
    while (std::optional<PerceptionFrame> frame{m_queue.pop()}) {
        emit frameReady(*frame);
    }
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Core/CMakeLists.txt`, add `src/perceptionpipe.cpp` and
`src/perceptionpipe.h` to `SOURCES`.

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(perceptionpipe Core)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 7: Commit**

```bash
git add modules/Core tests
git commit -m "Add the pipe that carries frames across threads without growing"
```

---

### Task 8: ResourceMonitor

The instrument that would have caught the failure this rework exists to fix.

**Files:**
- Create: `modules/Core/src/resourcemonitor.h`
- Create: `modules/Core/src/resourcemonitor.cpp`
- Create: `tests/tst_resourcemonitor.cpp`
- Modify: `modules/Core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Kerfur::QueueStats`, `Kerfur::QueueStatsSource` (Task 3).
- Produces: `class Kerfur::ResourceMonitor : public QObject` as a QML singleton
  with `static ResourceMonitor &instance()`,
  `static ResourceMonitor *create(QQmlEngine *, QJSEngine *)`,
  `void registerQueue(QueueStatsSource *source)`,
  `void unregisterQueue(QueueStatsSource *source)`,
  `Q_INVOKABLE void sampleNow()`, `void setSampleIntervalMs(int intervalMs)`,
  `QList<QueueStats> queueStats() const`, properties `residentSetSizeKb`,
  `openFileDescriptors`, `threadCount`, `uptimeSeconds`, and
  `Q_SIGNAL void sampled()`.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_resourcemonitor.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "boundedqueue.h"
#include "resourcemonitor.h"

#include <QSignalSpy>
#include <QTest>

#include <cstring>
#include <memory>

using Kerfur::BoundedQueue;
using Kerfur::QueueStats;
using Kerfur::ResourceMonitor;

class TestResourceMonitor : public QObject
{
    Q_OBJECT

private slots:
    void samplesTheProcessFootprint();
    void seesResidentMemoryGrow();
    void reportsRegisteredQueues();
    void forgetsAnUnregisteredQueue();
    void emitsSampledOnEverySample();
};

void TestResourceMonitor::samplesTheProcessFootprint()
{
#if !defined(Q_OS_LINUX)
    QSKIP("The process footprint is read from /proc, which only Linux has");
#endif
    ResourceMonitor monitor;
    monitor.sampleNow();

    QVERIFY(monitor.residentSetSizeKb() > 0);
    QVERIFY(monitor.openFileDescriptors() > 0);
    QVERIFY(monitor.threadCount() > 0);
    QVERIFY(monitor.uptimeSeconds() >= 0);
}

void TestResourceMonitor::seesResidentMemoryGrow()
{
#if !defined(Q_OS_LINUX)
    QSKIP("The process footprint is read from /proc, which only Linux has");
#endif
    ResourceMonitor monitor;
    monitor.sampleNow();
    const qint64 before{monitor.residentSetSizeKb()};

    // Touched, not just allocated: an untouched mapping never becomes resident,
    // so a monitor that watched virtual size instead would pass this while
    // being useless for catching a real leak.
    constexpr qsizetype blockSize{64 * 1024 * 1024};
    std::unique_ptr<char[]> block{new char[blockSize]};
    std::memset(block.get(), 1, blockSize);

    monitor.sampleNow();
    const qint64 after{monitor.residentSetSizeKb()};

    QVERIFY2(after - before > 32 * 1024,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before).arg(after)));
}

void TestResourceMonitor::reportsRegisteredQueues()
{
    ResourceMonitor monitor;
    BoundedQueue<int> queue{2, QStringLiteral("watched")};
    queue.push(1);
    queue.push(2);
    queue.push(3);

    monitor.registerQueue(&queue);
    const QList<QueueStats> stats{monitor.queueStats()};

    QCOMPARE(stats.size(), 1);
    QCOMPARE(stats.first().name, QStringLiteral("watched"));
    QCOMPARE(stats.first().dropped, 1U);
}

void TestResourceMonitor::forgetsAnUnregisteredQueue()
{
    ResourceMonitor monitor;
    BoundedQueue<int> queue{2, QStringLiteral("temporary")};
    monitor.registerQueue(&queue);
    monitor.unregisterQueue(&queue);

    // A queue that outlives its registration is a dangling pointer read on the
    // next sample, so unregistering has to actually remove it.
    QCOMPARE(monitor.queueStats().size(), 0);
}

void TestResourceMonitor::emitsSampledOnEverySample()
{
    ResourceMonitor monitor;
    QSignalSpy spy{&monitor, &ResourceMonitor::sampled};

    monitor.sampleNow();
    monitor.sampleNow();

    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(TestResourceMonitor)
#include "tst_resourcemonitor.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `resourcemonitor.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Core/src/resourcemonitor.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_RESOURCEMONITOR_H
#define KERFUR_RESOURCEMONITOR_H

#include "queuestatssource.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Watches the process's own footprint and every registered queue.
///
/// The build this replaces died after about a day with nothing recorded about
/// how it got there. This exists so that growth is a slope on a graph somebody
/// already looks at, well before it is an out of memory kill at three in the
/// morning. P4 publishes these as Home Assistant diagnostic entities.
class ResourceMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qint64 residentSetSizeKb READ residentSetSizeKb NOTIFY sampled)
    Q_PROPERTY(int openFileDescriptors READ openFileDescriptors NOTIFY sampled)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY sampled)
    Q_PROPERTY(qint64 uptimeSeconds READ uptimeSeconds NOTIFY sampled)

public:
    explicit ResourceMonitor(QObject *parent = nullptr);

    static ResourceMonitor &instance();
    static ResourceMonitor *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    qint64 residentSetSizeKb() const;
    int openFileDescriptors() const;
    int threadCount() const;
    qint64 uptimeSeconds() const;

    /// The caller keeps ownership and must unregister before the queue dies.
    /// Registration deliberately does not take ownership: queues are members of
    /// the workers that use them, and outliving that worker is the bug.
    void registerQueue(QueueStatsSource *source);
    void unregisterQueue(QueueStatsSource *source);

    QList<QueueStats> queueStats() const;

    /// A zero or negative interval stops periodic sampling.
    void setSampleIntervalMs(int intervalMs);

    Q_INVOKABLE void sampleNow();

signals:
    void sampled();

private:
    static qint64 readResidentSetSizeKb();
    static int countEntries(const QString &directoryPath);

    QTimer m_timer;
    QElapsedTimer m_uptime;
    QList<QueueStatsSource *> m_queues;
    qint64 m_residentSetSizeKb{-1};
    int m_openFileDescriptors{-1};
    int m_threadCount{-1};
};

} // namespace Kerfur

#endif // KERFUR_RESOURCEMONITOR_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Core/src/resourcemonitor.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "resourcemonitor.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QList>
#include <QQmlEngine>
#include <QString>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace Kerfur {

namespace {

// A minute is often enough to draw a slope over a day and rare enough that the
// sampling itself never shows up in a profile.
constexpr int defaultSampleIntervalMs{60000};

} // namespace

ResourceMonitor::ResourceMonitor(QObject *parent)
    : QObject{parent}
{
    m_uptime.start();
    m_timer.setInterval(defaultSampleIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ResourceMonitor::sampleNow);
}

ResourceMonitor &ResourceMonitor::instance()
{
    static ResourceMonitor monitor;
    return monitor;
}

ResourceMonitor *ResourceMonitor::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

qint64 ResourceMonitor::residentSetSizeKb() const
{
    return m_residentSetSizeKb;
}

int ResourceMonitor::openFileDescriptors() const
{
    return m_openFileDescriptors;
}

int ResourceMonitor::threadCount() const
{
    return m_threadCount;
}

qint64 ResourceMonitor::uptimeSeconds() const
{
    return m_uptime.elapsed() / 1000;
}

void ResourceMonitor::registerQueue(QueueStatsSource *source)
{
    if (source == nullptr || m_queues.contains(source)) {
        return;
    }
    m_queues.append(source);
}

void ResourceMonitor::unregisterQueue(QueueStatsSource *source)
{
    m_queues.removeAll(source);
}

QList<QueueStats> ResourceMonitor::queueStats() const
{
    QList<QueueStats> stats;
    stats.reserve(m_queues.size());
    for (const QueueStatsSource *source : m_queues) {
        stats.append(source->queueStats());
    }
    return stats;
}

void ResourceMonitor::setSampleIntervalMs(int intervalMs)
{
    if (intervalMs <= 0) {
        m_timer.stop();
        return;
    }
    m_timer.setInterval(intervalMs);
    m_timer.start();
}

void ResourceMonitor::sampleNow()
{
    m_residentSetSizeKb = readResidentSetSizeKb();
    m_openFileDescriptors = countEntries(QStringLiteral("/proc/self/fd"));
    m_threadCount = countEntries(QStringLiteral("/proc/self/task"));

    QString queueSummary;
    for (const QueueStats &stats : queueStats()) {
        queueSummary += QStringLiteral(" %1=%2/%3(peak %4, dropped %5)")
                            .arg(stats.name)
                            .arg(stats.size)
                            .arg(stats.capacity)
                            .arg(stats.highWaterMark)
                            .arg(stats.dropped);
    }

    qInfo().noquote() << QStringLiteral("resources rss=%1kB fd=%2 threads=%3 uptime=%4s%5")
                             .arg(m_residentSetSizeKb)
                             .arg(m_openFileDescriptors)
                             .arg(m_threadCount)
                             .arg(uptimeSeconds())
                             .arg(queueSummary);

    emit sampled();
}

qint64 ResourceMonitor::readResidentSetSizeKb()
{
#if defined(Q_OS_LINUX)
    QFile file{QStringLiteral("/proc/self/statm")};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }

    // statm is "size resident shared text lib data dt", in pages. The second
    // field is what actually costs memory; the first is address space and grows
    // for reasons that are not leaks.
    const QList<QByteArray> fields{file.readLine().simplified().split(' ')};
    if (fields.size() < 2) {
        return -1;
    }

    bool parsed{false};
    const qint64 pages{fields.at(1).toLongLong(&parsed)};
    if (!parsed) {
        return -1;
    }

    const qint64 pageSizeKb{static_cast<qint64>(::sysconf(_SC_PAGESIZE)) / 1024};
    return pages * pageSizeKb;
#else
    return -1;
#endif
}

int ResourceMonitor::countEntries(const QString &directoryPath)
{
#if defined(Q_OS_LINUX)
    const QDir directory{directoryPath};
    if (!directory.exists()) {
        return -1;
    }
    return static_cast<int>(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
#else
    Q_UNUSED(directoryPath)
    return -1;
#endif
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Core/CMakeLists.txt`, add `src/resourcemonitor.cpp` and
`src/resourcemonitor.h` to `SOURCES`.

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(resourcemonitor Core)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 7`.

- [ ] **Step 7: Commit**

```bash
git add modules/Core tests
git commit -m "Add a resource monitor that makes growth visible before it kills"
```

---

### Task 9: GazeController

The eye movement control loop, taken out of QML. `Face.qml` currently runs the
arithmetic, the smoothing factor and the centre return timer in declarative code,
where none of it can be tested and the smoothing depends on how often a signal
happens to fire.

**Files:**
- Create: `modules/Behaviour/CMakeLists.txt`
- Create: `modules/Behaviour/src/gazecontroller.h`
- Create: `modules/Behaviour/src/gazecontroller.cpp`
- Create: `tests/tst_gazecontroller.cpp`
- Modify: `modules/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks. Deliberately free of Qt object machinery
  so it can be tested as pure arithmetic.
- Produces: `class Kerfur::GazeController` with
  `struct Angles { qreal leftRight; qreal upDown; }`,
  `void setResponseTimeSeconds(qreal seconds)`, `void setMovementRange(qreal range)`,
  `void setTarget(const QPointF &normalizedTarget)`, `void clearTarget()`,
  `void snapTo(const Angles &angles)`, `Angles advance(qreal deltaSeconds)`,
  `Angles angles() const`, `bool hasTarget() const`, `bool isSettled() const`,
  and `static constexpr qreal centreAngle{90.0}`.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_gazecontroller.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "gazecontroller.h"

#include <QTest>

using Kerfur::GazeController;

namespace {

GazeController::Angles settle(GazeController &controller, int steps)
{
    GazeController::Angles angles{controller.angles()};
    for (int step{0}; step < steps; ++step) {
        angles = controller.advance(1.0 / 60.0);
    }
    return angles;
}

} // namespace

class TestGazeController : public QObject
{
    Q_OBJECT

private slots:
    void startsCentred();
    void staysCentredForACentredTarget();
    void convergesToTheFullRangeAtRangeOne();
    void scalesTheDeflectionByMovementRange();
    void returnsToCentreWhenTheTargetIsCleared();
    void neverLeavesTheValidAngleRange();
    void doesNotOvershootOnAHugeTimeStep();
    void reachesTheSamePlaceRegardlessOfStepSize();
};

void TestGazeController::startsCentred()
{
    GazeController controller;
    QCOMPARE(controller.angles().leftRight, GazeController::centreAngle);
    QCOMPARE(controller.angles().upDown, GazeController::centreAngle);
    QVERIFY(!controller.hasTarget());
    QVERIFY(controller.isSettled());
}

void TestGazeController::staysCentredForACentredTarget()
{
    GazeController controller;
    controller.setTarget(QPointF{0.5, 0.5});
    const GazeController::Angles angles{settle(controller, 300)};

    QVERIFY(qAbs(angles.leftRight - 90.0) < 0.01);
    QVERIFY(qAbs(angles.upDown - 90.0) < 0.01);
}

void TestGazeController::convergesToTheFullRangeAtRangeOne()
{
    GazeController controller;
    controller.setMovementRange(1.0);
    controller.setTarget(QPointF{1.0, 0.0});
    const GazeController::Angles angles{settle(controller, 600)};

    QVERIFY2(qAbs(angles.leftRight - 180.0) < 0.5, qPrintable(QString::number(angles.leftRight)));
    QVERIFY2(qAbs(angles.upDown - 0.0) < 0.5, qPrintable(QString::number(angles.upDown)));
}

void TestGazeController::scalesTheDeflectionByMovementRange()
{
    GazeController controller;
    controller.setMovementRange(0.5);
    controller.setTarget(QPointF{1.0, 0.5});
    const GazeController::Angles angles{settle(controller, 600)};

    // Half the range means half the deflection from centre: 90 + 0.5 * 90.
    QVERIFY2(qAbs(angles.leftRight - 135.0) < 0.5, qPrintable(QString::number(angles.leftRight)));
}

void TestGazeController::returnsToCentreWhenTheTargetIsCleared()
{
    GazeController controller;
    controller.setMovementRange(1.0);
    controller.setTarget(QPointF{1.0, 1.0});
    settle(controller, 600);

    controller.clearTarget();
    const GazeController::Angles angles{settle(controller, 600)};

    QVERIFY(qAbs(angles.leftRight - 90.0) < 0.5);
    QVERIFY(qAbs(angles.upDown - 90.0) < 0.5);
    QVERIFY(controller.isSettled());
}

void TestGazeController::neverLeavesTheValidAngleRange()
{
    GazeController controller;
    controller.setMovementRange(4.0);

    // A range above one is a user setting, not a bug, so the clamp is what
    // keeps the shader's angle inputs meaningful.
    controller.setTarget(QPointF{2.0, -1.0});
    const GazeController::Angles angles{settle(controller, 600)};

    QVERIFY(angles.leftRight <= 180.0);
    QVERIFY(angles.leftRight >= 0.0);
    QVERIFY(angles.upDown <= 180.0);
    QVERIFY(angles.upDown >= 0.0);
}

void TestGazeController::doesNotOvershootOnAHugeTimeStep()
{
    GazeController controller;
    controller.setMovementRange(1.0);
    controller.setTarget(QPointF{1.0, 0.5});

    // A stalled frame produces a delta of seconds. An unclamped exponential
    // step would fly past the target and oscillate.
    const GazeController::Angles angles{controller.advance(10.0)};

    QVERIFY(angles.leftRight <= 180.0);
    QVERIFY(angles.leftRight >= 90.0);
}

void TestGazeController::reachesTheSamePlaceRegardlessOfStepSize()
{
    GazeController fine;
    fine.setMovementRange(1.0);
    fine.setTarget(QPointF{1.0, 0.5});
    for (int step{0}; step < 120; ++step) {
        fine.advance(1.0 / 120.0);
    }

    GazeController coarse;
    coarse.setMovementRange(1.0);
    coarse.setTarget(QPointF{1.0, 0.5});
    for (int step{0}; step < 30; ++step) {
        coarse.advance(1.0 / 30.0);
    }

    // Both advanced one second of simulated time. The old QML loop applied a
    // fixed factor per signal, so its speed depended on the detection rate.
    QVERIFY2(qAbs(fine.angles().leftRight - coarse.angles().leftRight) < 1.0,
             qPrintable(QStringLiteral("fine=%1 coarse=%2")
                            .arg(fine.angles().leftRight)
                            .arg(coarse.angles().leftRight)));
}

QTEST_APPLESS_MAIN(TestGazeController)
#include "tst_gazecontroller.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `gazecontroller.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Behaviour/src/gazecontroller.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_GAZECONTROLLER_H
#define KERFUR_GAZECONTROLLER_H

#include <QPointF>

namespace Kerfur {

/// Turns a point somebody is standing at into the two eye angles the face
/// renders, smoothly and independently of how often it is told.
///
/// This used to be arithmetic inside Face.qml, applying a fixed fraction of the
/// remaining distance every time a detection signal fired. That made the eyes
/// move at a speed set by the detection rate, so they were sluggish when
/// tracking was working well and jumpy when it was not. Here the smoothing is an
/// exponential approach over wall clock time, so the same second of movement
/// looks the same at any frame rate.
///
/// Deliberately not a QObject: it is pure arithmetic, and keeping it that way is
/// what makes the whole control loop testable without an event loop.
class GazeController
{
public:
    struct Angles
    {
        qreal leftRight{90.0};
        qreal upDown{90.0};
    };

    /// Straight ahead. The shader takes 0 to 180 with the middle as centre.
    static constexpr qreal centreAngle{90.0};

    /// Time to close roughly 63 percent of the remaining distance. Smaller is
    /// snappier.
    void setResponseTimeSeconds(qreal seconds);

    /// Scales how far from centre a target at the frame edge pushes the eyes.
    /// One means the edge of frame maps to the edge of travel.
    void setMovementRange(qreal range);

    /// `normalizedTarget` is in frame coordinates, with (0.5, 0.5) as centre.
    void setTarget(const QPointF &normalizedTarget);

    /// Stops following and begins drifting back to centre.
    void clearTarget();

    /// Jumps straight to `angles` without smoothing, for a manual override.
    void snapTo(const Angles &angles);

    /// Steps the loop forward and returns the new angles.
    Angles advance(qreal deltaSeconds);

    Angles angles() const;
    bool hasTarget() const;

    /// True when the eyes have effectively stopped moving, so the caller can
    /// stop ticking rather than run a timer forever.
    bool isSettled() const;

    qreal movementRange() const;

private:
    Angles targetAngles() const;

    QPointF m_target{0.5, 0.5};
    Angles m_angles;
    qreal m_responseTimeSeconds{0.18};
    qreal m_movementRange{1.0};
    bool m_hasTarget{false};
};

} // namespace Kerfur

#endif // KERFUR_GAZECONTROLLER_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Behaviour/src/gazecontroller.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "gazecontroller.h"

#include <QtMath>

#include <algorithm>

namespace Kerfur {

namespace {

// Below this the eyes are not moving in any way a viewer can see, so the caller
// is free to stop ticking.
constexpr qreal settledEpsilonDegrees{0.05};

constexpr qreal minimumResponseTimeSeconds{0.001};

qreal clampAngle(qreal angle)
{
    return std::clamp(angle, 0.0, 180.0);
}

} // namespace

void GazeController::setResponseTimeSeconds(qreal seconds)
{
    m_responseTimeSeconds = std::max(seconds, minimumResponseTimeSeconds);
}

void GazeController::setMovementRange(qreal range)
{
    m_movementRange = std::max(range, 0.0);
}

void GazeController::setTarget(const QPointF &normalizedTarget)
{
    m_target = normalizedTarget;
    m_hasTarget = true;
}

void GazeController::clearTarget()
{
    m_hasTarget = false;
}

void GazeController::snapTo(const Angles &angles)
{
    m_angles.leftRight = clampAngle(angles.leftRight);
    m_angles.upDown = clampAngle(angles.upDown);
}

GazeController::Angles GazeController::targetAngles() const
{
    if (!m_hasTarget) {
        return Angles{centreAngle, centreAngle};
    }

    // A target at the frame edge is half a frame from centre, so the full 180
    // degrees of travel is reached at range one. The old QML path halved this
    // for face tracking while the mouse path used the full sweep, which is why
    // the eyes followed a face less far than they followed the cursor.
    const qreal leftRight{centreAngle + (m_target.x() - 0.5) * 180.0 * m_movementRange};
    const qreal upDown{centreAngle + (m_target.y() - 0.5) * 180.0 * m_movementRange};
    return Angles{clampAngle(leftRight), clampAngle(upDown)};
}

GazeController::Angles GazeController::advance(qreal deltaSeconds)
{
    if (deltaSeconds <= 0.0) {
        return m_angles;
    }

    // Exponential approach. Clamped at one so a stalled frame that produces a
    // delta of seconds lands exactly on the target instead of flying past it.
    const qreal alpha{std::min(1.0 - qExp(-deltaSeconds / m_responseTimeSeconds), 1.0)};

    const Angles target{targetAngles()};
    m_angles.leftRight += (target.leftRight - m_angles.leftRight) * alpha;
    m_angles.upDown += (target.upDown - m_angles.upDown) * alpha;
    m_angles.leftRight = clampAngle(m_angles.leftRight);
    m_angles.upDown = clampAngle(m_angles.upDown);
    return m_angles;
}

GazeController::Angles GazeController::angles() const
{
    return m_angles;
}

bool GazeController::hasTarget() const
{
    return m_hasTarget;
}

qreal GazeController::movementRange() const
{
    return m_movementRange;
}

bool GazeController::isSettled() const
{
    const Angles target{targetAngles()};
    return qAbs(target.leftRight - m_angles.leftRight) < settledEpsilonDegrees
           && qAbs(target.upDown - m_angles.upDown) < settledEpsilonDegrees;
}

} // namespace Kerfur
```

- [ ] **Step 5: Create the Behaviour module target**

Create `modules/Behaviour/CMakeLists.txt`:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(BEHAVIOUR_MODULE Behaviour)

qt_add_library(${BEHAVIOUR_MODULE})

qt_add_qml_module(
    ${BEHAVIOUR_MODULE}
    URI "Behaviour"
    VERSION 1.0
    SOURCES src/gazecontroller.cpp
            src/gazecontroller.h
)

target_link_libraries(${BEHAVIOUR_MODULE} PUBLIC Qt6::Core Qt6::Qml Core)

target_include_directories(${BEHAVIOUR_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

In `modules/CMakeLists.txt`, add `add_subdirectory(Behaviour)` after the `Core`
line.

- [ ] **Step 6: Register the suite**

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(gazecontroller Behaviour)
```

- [ ] **Step 7: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 8: Commit**

```bash
git add modules/Behaviour modules/CMakeLists.txt tests
git commit -m "Move the gaze control loop out of QML into testable arithmetic"
```

---

### Task 10: BehaviourEngine

The singleton the face binds to. In P1 it enters `Sleeping`, `Idle`, `Attentive`,
`Engaged` and `Listening`. `Greeting` and `Reacting` are declared here because
they are part of the state set the face has to render, but the transitions into
them need identity change tracking and Home Assistant commands, so P5 adds them.

**Files:**
- Create: `modules/Behaviour/src/behaviourengine.h`
- Create: `modules/Behaviour/src/behaviourengine.cpp`
- Create: `tests/tst_behaviourengine.cpp`
- Modify: `modules/Behaviour/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Kerfur::GazeController` (Task 9), `Kerfur::WorldModel` (Task 6).
- Produces: `class Kerfur::BehaviourEngine : public QObject` as a QML singleton
  with `enum class State { Sleeping, Idle, Attentive, Greeting, Engaged, Listening, Reacting }`,
  `explicit BehaviourEngine(WorldModel &world, QObject *parent = nullptr)`,
  `static BehaviourEngine &instance()`,
  `static BehaviourEngine *create(QQmlEngine *, QJSEngine *)`,
  properties `state`, `gazeLeftRight`, `gazeUpDown`, `tracking`, `movementRange`,
  `engageProximity`, slots `lookAt(const QPointF &)`, `releaseGaze()`,
  `setSleepAfterMs(int)`, and `Q_SIGNAL void meowRequested()`.

- [ ] **Step 1: Write the failing test**

Create `tests/tst_behaviourengine.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "behaviourengine.h"
#include "perceptiontypes.h"
#include "worldmodel.h"

#include <QSignalSpy>
#include <QTest>

using Kerfur::BehaviourEngine;
using Kerfur::PerceptionFrame;
using Kerfur::TrackedPerson;
using Kerfur::WorldModel;

static PerceptionFrame frameWith(qreal proximity, const QPointF &head)
{
    TrackedPerson person;
    person.setTrackId(1);
    person.setProximity(proximity);
    person.setHeadCenter(head);
    person.setBoundingBox(QRectF{0.3, 0.1, 0.2, proximity});

    PerceptionFrame frame;
    frame.setPeople({person});
    return frame;
}

class TestBehaviourEngine : public QObject
{
    Q_OBJECT

private slots:
    void startsIdleAndCentred();
    void becomesAttentiveForADistantPerson();
    void becomesEngagedForAClosePerson();
    void followsTheNearestHead();
    void returnsToIdleAndCentreWhenEverybodyLeaves();
    void entersListeningOnTheWakeWord();
    void fallsAsleepAfterTheConfiguredQuiet();
    void honoursAManualLookWhenNobodyIsSeen();
    void ignoresAManualLookWhileSomebodyIsSeen();
    void roundTripsAWrittenGazeAngle();
};

void TestBehaviourEngine::startsIdleAndCentred()
{
    WorldModel world;
    BehaviourEngine engine{world};

    QCOMPARE(engine.state(), BehaviourEngine::State::Idle);
    QCOMPARE(engine.gazeLeftRight(), 90.0);
    QVERIFY(!engine.isTracking());
}

void TestBehaviourEngine::becomesAttentiveForADistantPerson()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setEngageProximity(0.6);

    world.applyPerceptionFrame(frameWith(0.2, QPointF{0.5, 0.4}));

    QCOMPARE(engine.state(), BehaviourEngine::State::Attentive);
    QVERIFY(engine.isTracking());
}

void TestBehaviourEngine::becomesEngagedForAClosePerson()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setEngageProximity(0.6);

    world.applyPerceptionFrame(frameWith(0.8, QPointF{0.5, 0.4}));

    QCOMPARE(engine.state(), BehaviourEngine::State::Engaged);
}

void TestBehaviourEngine::followsTheNearestHead()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setMovementRange(1.0);

    world.applyPerceptionFrame(frameWith(0.3, QPointF{1.0, 0.5}));

    QTRY_VERIFY_WITH_TIMEOUT(engine.gazeLeftRight() > 170.0, 4000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.gazeUpDown() - 90.0) < 2.0, 4000);
}

void TestBehaviourEngine::returnsToIdleAndCentreWhenEverybodyLeaves()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setMovementRange(1.0);

    world.applyPerceptionFrame(frameWith(0.3, QPointF{1.0, 0.5}));
    QTRY_VERIFY_WITH_TIMEOUT(engine.gazeLeftRight() > 170.0, 4000);

    world.applyPerceptionFrame(PerceptionFrame{});

    QCOMPARE(engine.state(), BehaviourEngine::State::Idle);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.gazeLeftRight() - 90.0) < 2.0, 4000);
}

void TestBehaviourEngine::entersListeningOnTheWakeWord()
{
    WorldModel world;
    BehaviourEngine engine{world};

    world.setWakeWordActive(true);
    QCOMPARE(engine.state(), BehaviourEngine::State::Listening);

    world.setWakeWordActive(false);
    QCOMPARE(engine.state(), BehaviourEngine::State::Idle);
}

void TestBehaviourEngine::fallsAsleepAfterTheConfiguredQuiet()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setSleepAfterMs(80);

    QTRY_COMPARE_WITH_TIMEOUT(engine.state(), BehaviourEngine::State::Sleeping, 4000);

    // Somebody walking in wakes it immediately rather than after another tick.
    world.applyPerceptionFrame(frameWith(0.2, QPointF{0.5, 0.5}));
    QCOMPARE(engine.state(), BehaviourEngine::State::Attentive);
}

void TestBehaviourEngine::honoursAManualLookWhenNobodyIsSeen()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setMovementRange(1.0);

    engine.lookAt(QPointF{0.0, 0.5});
    QTRY_VERIFY_WITH_TIMEOUT(engine.gazeLeftRight() < 10.0, 4000);
}

void TestBehaviourEngine::ignoresAManualLookWhileSomebodyIsSeen()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setMovementRange(1.0);

    world.applyPerceptionFrame(frameWith(0.3, QPointF{1.0, 0.5}));
    engine.lookAt(QPointF{0.0, 0.5});

    // A person in frame outranks the mouse, which is how the face behaved
    // before and is what stops the cursor fighting the tracker.
    QTRY_VERIFY_WITH_TIMEOUT(engine.gazeLeftRight() > 170.0, 4000);
}

void TestBehaviourEngine::roundTripsAWrittenGazeAngle()
{
    WorldModel world;
    BehaviourEngine engine{world};
    engine.setMovementRange(1.0);

    // The control panel sliders write the angle directly through KSettings, so
    // a written angle has to stay put rather than drift back to centre.
    engine.setGazeLeftRight(135.0);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.gazeLeftRight() - 135.0) < 2.0, 4000);
}

QTEST_GUILESS_MAIN(TestBehaviourEngine)
#include "tst_behaviourengine.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run `make test`.
Expected: the build fails with `behaviourengine.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `modules/Behaviour/src/behaviourengine.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_BEHAVIOURENGINE_H
#define KERFUR_BEHAVIOURENGINE_H

#include "gazecontroller.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

class WorldModel;

/// Turns what Kerfur perceives into how Kerfur behaves.
///
/// The face renders; this decides. Before this existed, Face.qml made the
/// decisions itself in signal handlers, which is why none of it could be tested
/// and why the eye movement speed depended on the detection rate.
///
/// P1 enters Sleeping, Idle, Attentive, Engaged and Listening. Greeting and
/// Reacting are declared because the face has to be able to render them, but
/// entering them needs identity change tracking and Home Assistant commands,
/// which P5 adds.
class BehaviourEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum class State { Sleeping, Idle, Attentive, Greeting, Engaged, Listening, Reacting };
    Q_ENUM(State)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qreal gazeLeftRight READ gazeLeftRight WRITE setGazeLeftRight NOTIFY gazeChanged)
    Q_PROPERTY(qreal gazeUpDown READ gazeUpDown WRITE setGazeUpDown NOTIFY gazeChanged)
    Q_PROPERTY(bool tracking READ isTracking NOTIFY trackingChanged)
    Q_PROPERTY(qreal movementRange READ movementRange WRITE setMovementRange NOTIFY
                   movementRangeChanged)
    Q_PROPERTY(qreal engageProximity READ engageProximity WRITE setEngageProximity NOTIFY
                   engageProximityChanged)

    explicit BehaviourEngine(WorldModel &world, QObject *parent = nullptr);

    static BehaviourEngine &instance();
    static BehaviourEngine *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    State state() const;
    qreal gazeLeftRight() const;
    qreal gazeUpDown() const;

    /// True while a real person, rather than the mouse, is driving the gaze.
    bool isTracking() const;

    qreal movementRange() const;
    qreal engageProximity() const;

public slots:
    /// A manual gaze target in frame coordinates. Honoured only while nobody is
    /// being tracked, so the cursor never fights the camera.
    void lookAt(const QPointF &normalizedTarget);

    void releaseGaze();
    void setGazeLeftRight(qreal angle);
    void setGazeUpDown(qreal angle);
    void setMovementRange(qreal range);

    /// The share of frame height above which a person counts as close enough to
    /// engage with rather than merely watch.
    void setEngageProximity(qreal proximity);

    /// Quiet time before dropping to Sleeping. Exposed so a test does not have
    /// to wait out the real five minutes.
    void setSleepAfterMs(int milliseconds);

signals:
    void stateChanged();
    void gazeChanged();
    void trackingChanged();
    void movementRangeChanged();
    void engageProximityChanged();
    void meowRequested();

private:
    void onPerceptionChanged();
    void onWakeWordChanged();
    void reevaluateState();
    void setState(State state);
    void tick();
    void ensureTicking();

    WorldModel &m_world;
    GazeController m_gaze;

    // Two cadences on purpose. The gaze loop needs sixty steps a second while
    // the eyes are moving and none at all once they settle; the state machine
    // needs a slow heartbeat so that Sleeping, which is a timeout rather than an
    // event, is noticed without keeping the fast timer alive all night.
    QTimer m_tickTimer;
    QTimer m_stateTimer;

    QElapsedTimer m_sinceLastTick;
    QElapsedTimer m_sinceLastPresence;
    QPointF m_manualTarget{0.5, 0.5};
    State m_state{State::Idle};
    qreal m_engageProximity{0.45};
    int m_sleepAfterMs{300000};
    bool m_tracking{false};
    bool m_hasManualTarget{false};
};

} // namespace Kerfur

#endif // KERFUR_BEHAVIOURENGINE_H
```

- [ ] **Step 4: Write the implementation**

Create `modules/Behaviour/src/behaviourengine.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "behaviourengine.h"

#include "worldmodel.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QtMath>

#include <algorithm>

namespace Kerfur {

namespace {

// About sixty ticks a second while anything is moving. The timer stops as soon
// as the eyes settle, so an idle Kerfur is not spinning a timer all night.
constexpr int tickIntervalMs{16};

// The heartbeat that notices a timeout. A second is far finer than the five
// minute quiet it is watching for, and cheap enough to leave running.
constexpr int stateIntervalMs{1000};

} // namespace

BehaviourEngine::BehaviourEngine(WorldModel &world, QObject *parent)
    : QObject{parent}
    , m_world{world}
{
    m_sinceLastTick.start();
    m_sinceLastPresence.start();

    m_tickTimer.setInterval(tickIntervalMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &BehaviourEngine::tick);

    m_stateTimer.setInterval(stateIntervalMs);
    connect(&m_stateTimer, &QTimer::timeout, this, &BehaviourEngine::reevaluateState);
    m_stateTimer.start();

    connect(&m_world, &WorldModel::perceptionChanged, this,
            &BehaviourEngine::onPerceptionChanged);
    connect(&m_world, &WorldModel::wakeWordActiveChanged, this,
            &BehaviourEngine::onWakeWordChanged);

    reevaluateState();
}

BehaviourEngine &BehaviourEngine::instance()
{
    static BehaviourEngine engine{WorldModel::instance()};
    return engine;
}

BehaviourEngine *BehaviourEngine::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

BehaviourEngine::State BehaviourEngine::state() const
{
    return m_state;
}

qreal BehaviourEngine::gazeLeftRight() const
{
    return m_gaze.angles().leftRight;
}

qreal BehaviourEngine::gazeUpDown() const
{
    return m_gaze.angles().upDown;
}

bool BehaviourEngine::isTracking() const
{
    return m_tracking;
}

qreal BehaviourEngine::movementRange() const
{
    return m_gaze.movementRange();
}

qreal BehaviourEngine::engageProximity() const
{
    return m_engageProximity;
}

void BehaviourEngine::onPerceptionChanged()
{
    if (m_world.hasNearestPerson()) {
        m_sinceLastPresence.restart();

        const QPointF head{m_world.nearestHeadCenter()};
        // A person with no confident facial keypoints still has a body. Aim at
        // the top of the box rather than dropping the target, so the eyes do
        // not snap to centre every time somebody turns their head away.
        const QPointF target{head.isNull()
                                 ? QPointF{m_world.nearestPerson().boundingBox().center().x(),
                                           m_world.nearestPerson().boundingBox().top()}
                                 : head};
        m_gaze.setTarget(target);

        if (!m_tracking) {
            m_tracking = true;
            emit trackingChanged();
        }
    } else {
        if (m_tracking) {
            m_tracking = false;
            emit trackingChanged();
        }
        if (m_hasManualTarget) {
            m_gaze.setTarget(m_manualTarget);
        } else {
            m_gaze.clearTarget();
        }
    }

    reevaluateState();
    ensureTicking();
}

void BehaviourEngine::onWakeWordChanged()
{
    reevaluateState();

    // Hearing its own name is the one thing Kerfur reacts to out loud in P1.
    // Face.qml connects to this rather than to the world model directly, so
    // that P5 can add greetings and reactions without touching the face again.
    if (m_world.isWakeWordActive()) {
        emit meowRequested();
    }
}

void BehaviourEngine::reevaluateState()
{
    if (m_world.isWakeWordActive()) {
        setState(State::Listening);
        return;
    }

    if (m_world.hasNearestPerson()) {
        const bool close{m_world.nearestPerson().proximity() >= m_engageProximity};
        setState(close ? State::Engaged : State::Attentive);
        return;
    }

    if (m_sinceLastPresence.elapsed() >= m_sleepAfterMs) {
        setState(State::Sleeping);
        return;
    }

    setState(State::Idle);
}

void BehaviourEngine::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void BehaviourEngine::tick()
{
    const qreal deltaSeconds{static_cast<qreal>(m_sinceLastTick.restart()) / 1000.0};
    const GazeController::Angles before{m_gaze.angles()};
    const GazeController::Angles after{m_gaze.advance(deltaSeconds)};

    if (!qFuzzyCompare(before.leftRight, after.leftRight)
        || !qFuzzyCompare(before.upDown, after.upDown)) {
        emit gazeChanged();
    }

    // Stops as soon as the eyes are still. Anything that moves the target calls
    // ensureTicking, so an idle Kerfur is not spinning a sixty hertz timer for
    // the whole night.
    if (m_gaze.isSettled()) {
        m_tickTimer.stop();
    }
}

void BehaviourEngine::ensureTicking()
{
    if (!m_tickTimer.isActive()) {
        m_sinceLastTick.restart();
        m_tickTimer.start();
    }
}

void BehaviourEngine::lookAt(const QPointF &normalizedTarget)
{
    m_manualTarget = normalizedTarget;
    m_hasManualTarget = true;

    if (!m_tracking) {
        m_gaze.setTarget(normalizedTarget);
        ensureTicking();
    }
}

void BehaviourEngine::releaseGaze()
{
    m_hasManualTarget = false;
    if (!m_tracking) {
        m_gaze.clearTarget();
        ensureTicking();
    }
}

void BehaviourEngine::setGazeLeftRight(qreal angle)
{
    const qreal range{std::max(m_gaze.movementRange(), 0.0001)};
    QPointF target{m_hasManualTarget ? m_manualTarget : QPointF{0.5, 0.5}};
    target.setX(0.5 + (angle - GazeController::centreAngle) / (180.0 * range));
    lookAt(target);
}

void BehaviourEngine::setGazeUpDown(qreal angle)
{
    const qreal range{std::max(m_gaze.movementRange(), 0.0001)};
    QPointF target{m_hasManualTarget ? m_manualTarget : QPointF{0.5, 0.5}};
    target.setY(0.5 + (angle - GazeController::centreAngle) / (180.0 * range));
    lookAt(target);
}

void BehaviourEngine::setMovementRange(qreal range)
{
    if (qFuzzyCompare(m_gaze.movementRange(), range)) {
        return;
    }
    m_gaze.setMovementRange(range);
    ensureTicking();
    emit movementRangeChanged();
}

void BehaviourEngine::setEngageProximity(qreal proximity)
{
    if (qFuzzyCompare(m_engageProximity, proximity)) {
        return;
    }
    m_engageProximity = proximity;
    reevaluateState();
    emit engageProximityChanged();
}

void BehaviourEngine::setSleepAfterMs(int milliseconds)
{
    m_sleepAfterMs = milliseconds;
    reevaluateState();
}

} // namespace Kerfur
```

- [ ] **Step 5: Add the sources to the module and the suite**

In `modules/Behaviour/CMakeLists.txt`, add `src/behaviourengine.cpp` and
`src/behaviourengine.h` to `SOURCES`.

In `tests/CMakeLists.txt` add:

```cmake
kerfur_add_test(behaviourengine Behaviour)
```

- [ ] **Step 6: Run the tests to verify they pass**

Ask the user to run `make test`.
Expected: `100% tests passed, 0 tests failed out of 9`.

- [ ] **Step 7: Commit**

```bash
git add modules/Behaviour tests
git commit -m "Add the behaviour engine that decides where Kerfur looks"
```

---

### Task 11: Rewire the control panel

`ControlPanel.qml` is done first because `Main.qml` hands it a `FaceTracker` that
Task 12 removes. Doing the face first would leave the panel unable to instantiate.

**Files:**
- Modify: `modules/Tools/qml/ControlPanel.qml`
- Modify: `qml/Main.qml:38-45`
- Modify: `modules/Tools/CMakeLists.txt`

**Interfaces:**
- Consumes: `WorldModel`, `ResourceMonitor` (Core), `BehaviourEngine` (Behaviour).
- Produces: a `ControlPanel` with no required properties.

- [ ] **Step 1: Repoint the module dependency**

In `modules/Tools/CMakeLists.txt`, replace `DEPENDENCIES TARGET Vision` with:

```cmake
    DEPENDENCIES TARGET Core Behaviour
```

and add below the `qt_add_qml_module` call:

```cmake
target_link_libraries(${TOOLS_MODULE} PRIVATE Core Behaviour)
```

- [ ] **Step 2: Replace the imports and drop the required property**

In `modules/Tools/qml/ControlPanel.qml`, replace lines 1 to 11 with:

```qml
import Behaviour
import Core
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import qml 1.0

Rectangle {
    id: root

```

- [ ] **Step 3: Replace the camera feed block with a perception readout**

Replace the `CameraFeedDisplay { ... }` block at what was line 109 with:

```qml
            Column {
                spacing: 2
                width: parent.width - 20

                Text {
                    color: "white"
                    text: "Camera: " + (WorldModel.cameraOnline ? "online" : "offline")
                }

                Text {
                    color: "white"
                    text: "People: " + WorldModel.personCount
                }

                Text {
                    color: "white"
                    text: "Identity: " + (WorldModel.hasNearestPerson && WorldModel.nearestPerson.known ? WorldModel.nearestPerson.identity : "-")
                }

                Text {
                    color: "white"
                    text: "Emotion: " + (WorldModel.hasNearestPerson ? WorldModel.nearestPerson.emotion : "-")
                }

                Text {
                    color: "white"
                    text: "State: " + BehaviourEngine.state
                }
            }
```

- [ ] **Step 4: Replace the detection sliders**

The `detectionConfidence` and `trackingSmoothing` sliders drove `FaceTracker`
properties that no longer exist. Replace both slider blocks (what were lines 141
to 168, including their label `Text` items) with a single engage threshold
control:

```qml
            Text {
                color: "white"
                text: "Engage proximity: " + BehaviourEngine.engageProximity.toFixed(2)
            }

            Slider {
                from: 0.1
                to: 1.0
                value: BehaviourEngine.engageProximity
                width: parent.width - 20

                onMoved: BehaviourEngine.engageProximity = value
            }
```

- [ ] **Step 5: Replace the speech section**

Replace the microphone device row, the `SpeechDisplay` and the inline
`SpeechRecognizer` (what were roughly lines 239 to 332) with:

```qml
            Text {
                color: "white"
                text: "Microphone: " + (WorldModel.microphoneOnline ? "online" : "offline")
            }

            Text {
                color: "white"
                text: "Wake word: " + (WorldModel.wakeWordActive ? "active" : "idle")
            }

            Text {
                color: "white"
                text: "Heard: " + WorldModel.heardText
                width: parent.width - 20
                wrapMode: Text.Wrap
            }
```

Device selection moves to the Hearing module in P3, which is where the device
list will exist again.

- [ ] **Step 6: Add the resource readout**

Immediately before the closing brace of `controlsColumn`, add:

```qml
            Text {
                color: "white"
                font.bold: true
                text: "Resources"
                topPadding: 15
            }

            Text {
                color: "white"
                text: "RSS: " + ResourceMonitor.residentSetSizeKb + " kB"
            }

            Text {
                color: "white"
                text: "Descriptors: " + ResourceMonitor.openFileDescriptors + "  Threads: " + ResourceMonitor.threadCount
            }

            Text {
                color: "white"
                text: "Uptime: " + ResourceMonitor.uptimeSeconds + " s"
            }
```

- [ ] **Step 7: Stop threading the tracker through Main.qml**

In `qml/Main.qml`, delete the line `faceTracker: face.faceTracker` from the
`ControlPanel` block.

- [ ] **Step 8: Ask the user to build and run**

Tell the user: "Task 11 rewires the control panel onto WorldModel. Please run
`make QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64`, run the app, press Tab, and confirm the
panel opens with the perception and resource readouts showing and no QML warnings
in the console."

- [ ] **Step 9: Commit**

```bash
git add modules/Tools qml/Main.qml
git commit -m "Read the control panel from the world model instead of the tracker"
```

---

### Task 12: Rewire the face

**Files:**
- Modify: `modules/Display/qml/Face.qml`
- Modify: `modules/Display/CMakeLists.txt`

**Interfaces:**
- Consumes: `WorldModel` (Core), `BehaviourEngine` (Behaviour).
- Produces: a `Face` with no `faceTracker` alias.

- [ ] **Step 1: Repoint the module dependency**

In `modules/Display/CMakeLists.txt`, replace `DEPENDENCIES TARGET Tools Vision`
with:

```cmake
    DEPENDENCIES TARGET Tools Core Behaviour
```

and add below the `qt_add_qml_module` call:

```cmake
target_link_libraries(${DISPLAY_MODULE} PRIVATE Core Behaviour)
```

- [ ] **Step 2: Replace the imports**

In `modules/Display/qml/Face.qml`, replace lines 1 to 9 with:

```qml
import Behaviour
import Core
import Qt.labs.synchronizer
import QtMultimedia
import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Tools
import qml 1.0
```

- [ ] **Step 3: Replace the tracking properties**

Delete `readonly property alias faceTracker: faceTracker` and
`property bool maintainEyePosition: ...`. Replace the `faceTrackingActive`
property with:

```qml
    readonly property bool faceTrackingActive: BehaviourEngine.tracking
```

Replace the two angle properties so the engine owns them:

```qml
    readonly property real targetLeftRightAngle: BehaviourEngine.gazeLeftRight
    readonly property real targetUpDownAngle: BehaviourEngine.gazeUpDown
```

- [ ] **Step 4: Repoint the settings synchronizers**

The two `Synchronizer` blocks two-way bind the angles to `KSettings`, which is how
the control panel sliders move the eyes. Point them at the engine:

```qml
    Synchronizer {
        id: leftRightSync

        sourceObject: BehaviourEngine
        sourceProperty: "gazeLeftRight"
        targetObject: KSettings
        targetProperty: "leftRightAngle"

        onValueBounced: (object, prop) => {
            console.log("LeftRight value bounced for", prop);
        }
    }

    Synchronizer {
        id: upDownSync

        sourceObject: BehaviourEngine
        sourceProperty: "gazeUpDown"
        targetObject: KSettings
        targetProperty: "upDownAngle"

        onValueBounced: (object, prop) => {
            console.log("UpDown value bounced for", prop);
        }
    }
```

- [ ] **Step 5: Replace the recognizer and tracker blocks**

Delete the whole `SpeechRecognizer { ... }` block, the whole `FaceTracker { ... }`
block, and the whole `centerReturnTimer` `Timer { ... }` block. The centre return
is now the gaze controller's job. In their place put:

```qml
    Connections {
        function onMeowRequested(): void {
            if (!root.isTouched && !root.isWinking && !root.isBlinking) {
                root.isTouched = true;
                root.playMeowSound();
            }
        }

        target: BehaviourEngine
    }
```

- [ ] **Step 6: Update Component.onCompleted**

The engine owns the angles now, so seeding `root` is meaningless. Replace the
handler with:

```qml
    Component.onCompleted: {
        BehaviourEngine.movementRange = KSettings.eyeMovementRange;
        BehaviourEngine.gazeLeftRight = KSettings.leftRightAngle;
        BehaviourEngine.gazeUpDown = KSettings.upDownAngle;
        root.resetBlinkTimer();
    }
```

- [ ] **Step 7: Repoint the mouse area**

In `pupilTrackingArea`, replace the two tracking properties and the two handlers:

```qml
        property bool shouldTrack: !root.isBlinking && !root.isTouched && (!pupilTrackingArea.pressed || root.isWinking)

        onMouseXChanged: {
            if (!BehaviourEngine.tracking && pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                BehaviourEngine.lookAt(Qt.point(pupilTrackingArea.mouseX / pupilTrackingArea.width, pupilTrackingArea.mouseY / pupilTrackingArea.height));
            }
        }
        onMouseYChanged: {
            if (!BehaviourEngine.tracking && pupilTrackingArea.shouldTrack && pupilTrackingArea.containsMouse) {
                BehaviourEngine.lookAt(Qt.point(pupilTrackingArea.mouseX / pupilTrackingArea.width, pupilTrackingArea.mouseY / pupilTrackingArea.height));
            }
        }
```

and change `hoverEnabled: !useFaceTracking && !maintainPosition` to:

```qml
        hoverEnabled: !BehaviourEngine.tracking
```

Delete the now unused `maintainPosition` and `useFaceTracking` properties.

- [ ] **Step 8: Repoint the status indicator**

Replace the indicator `Rectangle`'s three bindings:

```qml
        color: WorldModel.personPresent ? "lime" : (WorldModel.cameraOnline ? "orange" : "gray")
        visible: WorldModel.cameraOnline
```

and inside its `SequentialAnimation on opacity`:

```qml
            running: WorldModel.cameraOnline && !WorldModel.personPresent
```

- [ ] **Step 9: Ask the user to build and run**

Tell the user: "Task 12 rewires the face. Please run
`make QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64`, run the app, and confirm the eyes still
follow the mouse, blinking still happens, left click still meows, right click
still winks, and the control panel angle sliders still move the eyes. There should
be no QML warnings."

- [ ] **Step 10: Commit**

```bash
git add modules/Display
git commit -m "Drive the face from the behaviour engine and the world model"
```

---

### Task 13: Delete the old inference stack

Nothing references it now. Delete it rather than leaving it to rot beside its
replacement.

**Files:**
- Delete: `modules/Vision/src/AIFaceDetector.hpp`, `AIFaceDetector.cpp`,
  `FaceTracker.hpp`, `FaceTracker.cpp`, `modules/Vision/qml/CameraFeedDisplay.qml`
- Delete: `modules/Hearing/src/WhisperEngine.hpp`, `WhisperEngine.cpp`,
  `SpeechRecognizer.hpp`, `SpeechRecognizer.cpp`,
  `modules/Hearing/qml/SpeechDisplay.qml`
- Modify: `vcpkg.json`, `assets/models.txt`, `qml/KSettings.qml`,
  `CMakeLists.txt`, `modules/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: a tree with no OpenCV or whisper dependency and no orphaned settings.

- [ ] **Step 1: Delete the sources**

```bash
git rm modules/Vision/src/AIFaceDetector.hpp modules/Vision/src/AIFaceDetector.cpp \
       modules/Vision/src/FaceTracker.hpp modules/Vision/src/FaceTracker.cpp \
       modules/Vision/qml/CameraFeedDisplay.qml \
       modules/Hearing/src/WhisperEngine.hpp modules/Hearing/src/WhisperEngine.cpp \
       modules/Hearing/src/SpeechRecognizer.hpp modules/Hearing/src/SpeechRecognizer.cpp \
       modules/Hearing/qml/SpeechDisplay.qml
git rm modules/Vision/CMakeLists.txt modules/Hearing/CMakeLists.txt
```

- [ ] **Step 2: Stop building the deleted modules**

In `modules/CMakeLists.txt`, remove the `add_subdirectory(Vision)` and
`add_subdirectory(Hearing)` lines. Task 14 adds them back conditionally with new
contents.

In the top level `CMakeLists.txt`, remove `Vision` and `Hearing` from both the
`DEPENDENCIES TARGET` list of `qt_add_qml_module` and from
`target_link_libraries`, and add `Core` and `Behaviour` to both. Also remove
`${OpenCV_LIBS}` from `target_link_libraries` and the
`target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})` line,
since nothing in the application links OpenCV any more. P2 reintroduces it inside
the Vision module where it belongs.

- [ ] **Step 3: Prune the dependencies**

Replace `vcpkg.json` with:

```json
{
  "dependencies": [
    "opencv"
  ]
}
```

OpenCV stays because P2's Hailo pre and post processing needs its crop, letterbox,
resize and `warpAffine`. `whisper-cpp` and `ggml` have no remaining consumer.

- [ ] **Step 4: Empty the stale model manifest**

`assets/models.txt` lists two ONNX files and a whisper binary that nothing loads.
Replace its contents with a single line:

```
# Models are provisioned per device architecture from P2 onwards. See docs/superpowers/specs/2026-08-19-kerfur-perception-rework-design.md section 10.
```

- [ ] **Step 5: Drop the orphaned settings**

In `qml/KSettings.qml`, remove the `facesGalleryPath` and `modelsPath` properties
and their aliases in the `paths` Settings block, along with the now empty `paths`
block itself. P2 reintroduces the paths it actually needs. Leave
`faceTrackingEnabled`, `faceTrackingDebug`, `speechRecognitionEnabled` and
`speechRecognitionDebug`, which P2 and P3 consume.

- [ ] **Step 6: Ask the user to build and test**

Tell the user: "Task 13 deletes the old inference stack. Please run
`make clean-vcpkg && make QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64` and then `make test`,
and confirm both succeed. The vcpkg clean is needed because `vcpkg.json` changed."

- [ ] **Step 7: Commit**

```bash
git add -A modules vcpkg.json assets/models.txt qml/KSettings.qml CMakeLists.txt
git commit -m "Delete the CPU inference stack that the Hailo pipeline replaces"
```

---

### Task 14: Platform split

Three configurations from one tree. The `pi` configuration is validated on the
development machine here, while its modules are still empty, so that P2 starts
from a build that is known to configure rather than discovering the CMake wiring
and the Hailo integration are broken at the same time.

**Files:**
- Create: `cmake/platform.cmake`
- Create: `modules/Vision/CMakeLists.txt`
- Create: `modules/Vision/src/visionproducer.h`
- Create: `modules/Vision/src/visionproducer.cpp`
- Create: `modules/Hearing/CMakeLists.txt`
- Create: `modules/Hearing/src/hearingproducer.h`
- Create: `modules/Hearing/src/hearingproducer.cpp`
- Modify: `cmake/config.cmake`, `modules/CMakeLists.txt`, `CMakeLists.txt`,
  `src/main.cpp`, `Makefile`

**Interfaces:**
- Consumes: `Kerfur::PerceptionPipe`, `Kerfur::WorldModel`,
  `Kerfur::ResourceMonitor`.
- Produces: CMake variables `KERFUR_PLATFORM` (`pi`, `desktop` or `wasm`) and
  `KERFUR_HAS_PERCEPTION` (bool); `class Kerfur::VisionProducer` and
  `class Kerfur::HearingProducer`, each with
  `explicit <T>(WorldModel &world, QObject *parent = nullptr)`,
  `bool isEnabled() const` and `void setEnabled(bool)`.

- [ ] **Step 1: Write the platform resolver**

Create `cmake/platform.cmake`:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

# Resolves which Kerfur this build is. Perception modules exist only where the
# accelerator does, so a machine without one builds a face and a control panel
# and nothing else. The alternative, stub inference backends, would be code
# nobody runs pretending to be code somebody does.
if (NOT DEFINED KERFUR_PLATFORM OR KERFUR_PLATFORM STREQUAL "auto")
    if (EMSCRIPTEN)
        set(KERFUR_PLATFORM "wasm")
    else ()
        find_library(KERFUR_HAILORT_LIBRARY hailort)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND KERFUR_HAILORT_LIBRARY)
            set(KERFUR_PLATFORM "pi")
        else ()
            set(KERFUR_PLATFORM "desktop")
        endif ()
    endif ()
endif ()

set(KERFUR_VALID_PLATFORMS pi desktop wasm)
if (NOT KERFUR_PLATFORM IN_LIST KERFUR_VALID_PLATFORMS)
    message(
        FATAL_ERROR
        "KERFUR_PLATFORM is '${KERFUR_PLATFORM}'; expected one of ${KERFUR_VALID_PLATFORMS} or auto"
    )
endif ()

set(KERFUR_HAS_PERCEPTION OFF)
if (KERFUR_PLATFORM STREQUAL "pi")
    set(KERFUR_HAS_PERCEPTION ON)
endif ()

message(STATUS "KERFUR_PLATFORM=${KERFUR_PLATFORM}")
message(STATUS "KERFUR_HAS_PERCEPTION=${KERFUR_HAS_PERCEPTION}")
```

In `cmake/config.cmake`, add after the `include(...functions.cmake)` line:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform.cmake")
```

- [ ] **Step 2: Make the module list conditional**

Replace `modules/CMakeLists.txt` with:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

add_subdirectory(Core)
add_subdirectory(Behaviour)
add_subdirectory(Tools)
add_subdirectory(Display)

if (KERFUR_HAS_PERCEPTION)
    add_subdirectory(Vision)
    add_subdirectory(Hearing)
endif ()
```

In the top level `CMakeLists.txt`, make the application's dependency lists follow
suit. Replace the `DEPENDENCIES TARGET` block of `qt_add_qml_module` with:

```cmake
    DEPENDENCIES TARGET
                 Core
                 Behaviour
                 Tools
                 Display
                 ${KERFUR_PERCEPTION_TARGETS}
```

and add, just before `qt_add_qml_module`:

```cmake
set(KERFUR_PERCEPTION_TARGETS "")
if (KERFUR_HAS_PERCEPTION)
    set(KERFUR_PERCEPTION_TARGETS Vision Hearing)
endif ()
```

Add `${KERFUR_PERCEPTION_TARGETS}` to `target_link_libraries` alongside `Core`,
`Behaviour`, `Tools` and `Display`.

- [ ] **Step 3: Write the Vision producer shell**

Create `modules/Vision/src/visionproducer.h`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VISIONPRODUCER_H
#define KERFUR_VISIONPRODUCER_H

#include "perceptionpipe.h"

#include <QObject>

namespace Kerfur {

class WorldModel;

/// Owns the camera and the accelerator, and is the only writer of the visual
/// half of WorldModel.
///
/// In P1 it owns nothing but the pipe out. P2 puts libcamera capture and the
/// HailoRT inference worker behind it, both of which push through that same
/// pipe, so the thread crossing and the backpressure are settled before any
/// hardware is involved.
class VisionProducer : public QObject
{
    Q_OBJECT

public:
    explicit VisionProducer(WorldModel &world, QObject *parent = nullptr);
    ~VisionProducer() override;

    bool isEnabled() const;

public slots:
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

private:
    WorldModel &m_world;
    PerceptionPipe m_pipe;
    bool m_enabled{false};
};

} // namespace Kerfur

#endif // KERFUR_VISIONPRODUCER_H
```

Create `modules/Vision/src/visionproducer.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "visionproducer.h"

#include "resourcemonitor.h"
#include "worldmodel.h"

namespace Kerfur {

namespace {

// Two deep. One frame being worked on and one waiting is the whole useful
// backlog; anything more is latency nobody asked for.
constexpr qsizetype frameQueueDepth{2};

} // namespace

VisionProducer::VisionProducer(WorldModel &world, QObject *parent)
    : QObject{parent}
    , m_world{world}
    , m_pipe{frameQueueDepth, QStringLiteral("vision.frames"), this}
{
    connect(&m_pipe, &PerceptionPipe::frameReady, &m_world, &WorldModel::applyPerceptionFrame);
    ResourceMonitor::instance().registerQueue(m_pipe.statsSource());
}

VisionProducer::~VisionProducer()
{
    // The pipe dies with this object, so the monitor must stop holding a
    // pointer to its queue first.
    ResourceMonitor::instance().unregisterQueue(m_pipe.statsSource());
}

bool VisionProducer::isEnabled() const
{
    return m_enabled;
}

void VisionProducer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    // P2 starts and stops the camera and the inference worker here. Until then
    // the camera is honestly reported as offline whatever the setting says.
    m_world.setCameraOnline(false);
    emit enabledChanged();
}

} // namespace Kerfur
```

Create `modules/Vision/CMakeLists.txt`:

```cmake
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

set(VISION_MODULE Vision)

qt_add_library(${VISION_MODULE})

qt_add_qml_module(
    ${VISION_MODULE}
    URI "Vision"
    VERSION 1.0
    SOURCES src/visionproducer.cpp
            src/visionproducer.h
)

target_link_libraries(${VISION_MODULE} PUBLIC Qt6::Core Qt6::Qml Core)

target_include_directories(${VISION_MODULE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

- [ ] **Step 4: Write the Hearing producer shell**

Create `modules/Hearing/src/hearingproducer.h`, identical in shape to the Vision
one but without a pipe, since speech produces text rather than frames:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_HEARINGPRODUCER_H
#define KERFUR_HEARINGPRODUCER_H

#include <QObject>

namespace Kerfur {

class WorldModel;

/// Owns the microphone, and is the only writer of the audible half of
/// WorldModel.
///
/// P3 puts QAudioSource and Moonshine's streaming transcriber behind this.
class HearingProducer : public QObject
{
    Q_OBJECT

public:
    explicit HearingProducer(WorldModel &world, QObject *parent = nullptr);

    bool isEnabled() const;

public slots:
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

private:
    WorldModel &m_world;
    bool m_enabled{false};
};

} // namespace Kerfur

#endif // KERFUR_HEARINGPRODUCER_H
```

Create `modules/Hearing/src/hearingproducer.cpp`:

```cpp
// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "hearingproducer.h"

#include "worldmodel.h"

namespace Kerfur {

HearingProducer::HearingProducer(WorldModel &world, QObject *parent)
    : QObject{parent}
    , m_world{world}
{
}

bool HearingProducer::isEnabled() const
{
    return m_enabled;
}

void HearingProducer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    // P3 starts and stops the audio source and the transcriber here.
    m_world.setMicrophoneOnline(false);
    emit enabledChanged();
}

} // namespace Kerfur
```

Create `modules/Hearing/CMakeLists.txt` as a copy of the Vision one with
`HEARING_MODULE`, `URI "Hearing"`, and `src/hearingproducer.cpp` /
`src/hearingproducer.h` as its sources.

- [ ] **Step 5: Instantiate the producers and start the monitor**

In `src/main.cpp`, add after the `QQmlApplicationEngine engine;` line:

```cpp
    // Sampled far more often under the soak harness than in normal use, where a
    // minute is enough to draw a slope over a day.
    Kerfur::ResourceMonitor::instance().setSampleIntervalMs(
        qEnvironmentVariableIntValue("KERFUR_RESOURCE_SAMPLE_MS") > 0
            ? qEnvironmentVariableIntValue("KERFUR_RESOURCE_SAMPLE_MS")
            : 60000);

#ifdef KERFUR_HAS_PERCEPTION
    Kerfur::VisionProducer visionProducer{Kerfur::WorldModel::instance()};
    Kerfur::HearingProducer hearingProducer{Kerfur::WorldModel::instance()};
#endif
```

with the matching includes. Then add to `cmake/compilers.cmake`, at the top of
the file and outside every existing conditional, so the define follows the
platform rather than the operating system:

```cmake
if (KERFUR_HAS_PERCEPTION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE KERFUR_HAS_PERCEPTION)
endif ()
```

- [ ] **Step 6: Thread the platform through the Makefile**

In `Makefile`, add near the other project variables:

```make
KERFUR_PLATFORM ?= auto
```

and add `-DKERFUR_PLATFORM=$(KERFUR_PLATFORM) \` to the `deskop-configure` cmake
invocation.

- [ ] **Step 7: Ask the user to verify all three configurations**

Tell the user to run each of these and report:

```bash
cmake -S . -B build-check-desktop -DKERFUR_PLATFORM=desktop -DQT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64
cmake -S . -B build-check-pi -DKERFUR_PLATFORM=pi -DQT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64
cmake --build build-check-pi
make KERFUR_PLATFORM=desktop QT_ROOT_DIR=/opt/Qt/6.11.1/gcc_64
```

Expected: the desktop configure reports `KERFUR_HAS_PERCEPTION=OFF`, the pi
configure reports `ON` and then builds `Vision` and `Hearing` successfully even on
x86 (they have no Hailo dependency yet), and the full desktop build still runs.
Then remove the two check directories.

- [ ] **Step 8: Commit**

```bash
git add cmake modules CMakeLists.txt src/main.cpp Makefile
git commit -m "Split the build into pi, desktop and wasm configurations"
```

---

### Task 15: Display lifetime audit

The visual features are correct and are not being changed. This finds and fixes
the lifetime problems in them, and then measures rather than assuming.

**Files:**
- Modify: `modules/Display/qml/LedScreen.qml:37-40`
- Modify: `modules/Display/qml/Face.qml`

**Interfaces:**
- Consumes: `ResourceMonitor` (Task 8).
- Produces: no API change.

- [ ] **Step 1: Guard the null source dereference**

`LedScreen.qml` declares `property Item source: null` and then computes
`textureSize: Qt.size(root.source.width / root.ledScreenLedSize, ...)`. Every
evaluation while `source` is null is a TypeError, and the binding re-evaluates on
every size change. Replace the `ShaderEffectSource` block with:

```qml
        ShaderEffectSource {
            id: scaledSource

            // live only while something is on screen: a Kerfur asleep in a dark
            // room was still re-rendering this target every frame.
            hideSource: true
            live: root.visible && root.source !== null
            sourceItem: root.source
            textureSize: root.source ? Qt.size(root.source.width / root.ledScreenLedSize, root.source.height / root.ledScreenLedSize) : Qt.size(1, 1)
        }
```

- [ ] **Step 2: Make the meow sound source declarative**

`Face.qml` assigns `dynamicSound.source` imperatively in two places:
`playMeowSound()` and the `to: "meow"` transition's `ScriptAction`. Assigning a
source repeatedly re-resolves it on every meow. Give the `SoundEffect` its source
once:

```qml
    SoundEffect {
        id: dynamicSound

        source: root.pathSoundMeow
        volume: root.meowVolume
    }
```

Then in `playMeowSound()` remove the `dynamicSound.source = root.pathSoundMeow;`
line, leaving only `dynamicSound.play();`, and do the same in the `to: "meow"`
`ScriptAction`.

- [ ] **Step 3: Record the baseline**

Ask the user to run:

```bash
KERFUR_RESOURCE_SAMPLE_MS=10000 ./install/Kerfur 2>&1 | grep --line-buffered "^resources" | tee /tmp/kerfur-baseline.log
```

and to leave it running for thirty minutes with the face idle, then stop it and
report the first and last lines.

Expected: resident set size flat to within a few megabytes across the run. A
climbing figure here is a finding for step 4 rather than a failure of this task.

- [ ] **Step 4: Record the interactive figure**

Ask the user to repeat the same run for thirty minutes, this time with the control
panel open (Tab) and the mouse moving over the face periodically, and report the
first and last lines.

Expected: the same flat profile. The control panel is the path that binds the most
properties, and the mouse path is what drives the gaze loop, so this is where a
per-update allocation would show.

- [ ] **Step 5: Commit**

```bash
git add modules/Display
git commit -m "Stop the LED screen rendering into a null source and re-resolving the meow"
```

---

### Task 16: Soak harness and service unit

The last two of the five memory mechanisms in the design: an automated soak that
fails on a rising trend, and a service definition that turns any leak nobody
found into a restart instead of a kill.

**Files:**
- Create: `tools/soak.sh`
- Create: `deploy/kerfur.service`
- Modify: `Makefile`

**Interfaces:**
- Consumes: the `resources rss=...` log line from `ResourceMonitor::sampleNow()`
  (Task 8), and `KERFUR_RESOURCE_SAMPLE_MS` (Task 14).
- Produces: `make soak` and `make soak SOAK_SECONDS=86400`.

- [ ] **Step 1: Write the harness**

Create `tools/soak.sh`:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs Kerfur headless and fails if its resident set size trends upward.
#
# The failure this guards against took about a day to become visible, so the
# useful signal is the slope, not the peak: a process that has grown 40 MB and
# stopped is fine, and one growing 3 MB an hour is not, even though the second
# looks better after ten minutes.

set -euo pipefail

BINARY="${1:?usage: soak.sh <binary> [seconds] [max kB per hour]}"
DURATION="${2:-3600}"
MAX_GROWTH_KB_PER_HOUR="${3:-2048}"
SAMPLE_MS=10000
LOG="$(mktemp -t kerfur-soak-XXXXXX.log)"

echo "Soaking ${BINARY} for ${DURATION}s, failing above ${MAX_GROWTH_KB_PER_HOUR} kB/hour"
echo "Log: ${LOG}"

QT_QPA_PLATFORM=offscreen \
KERFUR_RESOURCE_SAMPLE_MS="${SAMPLE_MS}" \
    timeout --signal=TERM "${DURATION}" "${BINARY}" >"${LOG}" 2>&1 || true

grep "^resources" "${LOG}" \
    | sed -E 's/.*rss=([0-9-]+)kB.*uptime=([0-9]+)s.*/\2 \1/' \
    > "${LOG}.samples"

SAMPLE_COUNT="$(wc -l < "${LOG}.samples")"
if [ "${SAMPLE_COUNT}" -lt 6 ]; then
    echo "FAIL: only ${SAMPLE_COUNT} samples; the process did not stay up"
    exit 1
fi

# Least squares slope of rss against uptime, reported per hour. awk rather than
# first-versus-last because a single sample landing during a garbage collection
# would otherwise decide the verdict.
SLOPE="$(awk '
    { n++; sx += $1; sy += $2; sxx += $1 * $1; sxy += $1 * $2 }
    END {
        denominator = (n * sxx) - (sx * sx);
        if (denominator == 0) { print 0; exit }
        printf "%.1f", ((n * sxy) - (sx * sy)) / denominator * 3600;
    }
' "${LOG}.samples")"

echo "Samples: ${SAMPLE_COUNT}"
echo "First: $(head -1 "${LOG}.samples")"
echo "Last:  $(tail -1 "${LOG}.samples")"
echo "Slope: ${SLOPE} kB/hour"

if awk -v slope="${SLOPE}" -v limit="${MAX_GROWTH_KB_PER_HOUR}" \
       'BEGIN { exit (slope > limit) ? 0 : 1 }'; then
    echo "FAIL: resident set size is trending upward"
    exit 1
fi

echo "PASS"
```

Then make it executable:

```bash
chmod +x tools/soak.sh
```

- [ ] **Step 2: Add the make target**

In `Makefile`, after the `test:` target, add:

```make
SOAK_SECONDS ?= 3600

soak:
	tools/soak.sh "$(ABS_INSTALL_DIR)/$(PROJECT_TITLE)" $(SOAK_SECONDS)
```

- [ ] **Step 3: Write the service unit**

Create `deploy/kerfur.service`:

```ini
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later

[Unit]
Description=Kerfur
After=graphical.target

[Service]
Type=simple
ExecStart=/opt/kerfur/Kerfur
Restart=always
RestartSec=5

# Belt and braces for a leak nobody found. The process is expected to sit far
# below this on a 16 GB board, so reaching it means something is wrong and a
# restart is a better outcome than the kernel picking a victim.
MemoryMax=2G

# The watchdog covers the other failure: wedged rather than grown. Kerfur does
# not yet notify systemd, so this stays commented until the notify support lands
# with the Home Assistant bridge in P4.
# WatchdogSec=60
# NotifyAccess=main

[Install]
WantedBy=graphical.target
```

- [ ] **Step 4: Ask the user to run a short soak**

Tell the user: "Task 16 adds the soak harness. Please run `make soak
SOAK_SECONDS=900` and report the slope line."

Expected: `PASS`, with a slope near zero.

- [ ] **Step 5: Ask the user to run the full soak**

Tell the user: "This is the P1 definition of done. Please run `make soak
SOAK_SECONDS=86400` and report the result. It takes a day."

Expected: `PASS`.

- [ ] **Step 6: Commit**

```bash
git add tools deploy Makefile
git commit -m "Add a soak harness that fails on a rising memory trend"
```

---

## What this plan deliberately leaves out

Mechanism 2 of section 11 of the spec, RAII wrappers for every foreign handle,
has no task here because P1 introduces no foreign handles. HailoRT, libcamera,
Moonshine and paho all arrive in P2, P3 and P4, and each wrapper belongs in the
module that owns the handle. The rule is recorded in the spec and applies to
those tasks; inventing wrappers now for libraries not yet linked would be code
written against a guess at an API.

Model provisioning, section 10 of the spec, is likewise P2: `assets/models.txt`
is emptied here rather than replaced, because the manifest's contents depend on
the device architecture detection that does not exist until the Vision module is
real.

## Definition of done

P1 is complete when all of the following hold, matching section 13 of the spec:

- The `desktop` configuration builds and runs with no AI, and the face behaves
  correctly against an empty `WorldModel`: it blinks, follows the mouse, meows on
  left click and winks on right click.
- The `pi` configuration builds with the new `Vision` and `Hearing` modules
  present as empty producers.
- `Face.qml` and `ControlPanel.qml` read `WorldModel` and `BehaviourEngine`, and
  neither imports `Vision` or `Hearing`.
- The old Vision and Hearing implementations are deleted, and `vcpkg.json` no
  longer names `whisper-cpp` or `ggml`.
- `make test` passes all nine suites: harness, boundedqueue, perceptiontypes,
  peoplemodel, worldmodel, perceptionpipe, resourcemonitor, gazecontroller and
  behaviourengine.
- `make soak SOAK_SECONDS=86400` passes.
