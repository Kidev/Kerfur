// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FaceTracker.hpp"

#include "cameraselection.h"
#include "faceanalysisworker.h"
#include "identitylibrary.h"
#include "perceptiontypes.h"
#include "session.h"
#include "videoframeimage.h"

#include <QCameraFormat>
#include <QDebug>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QSet>
#include <QVariantMap>
#include <QVideoFrameFormat>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(trackerLog, "kerfur.vision.tracker")

/// The COCO bones, as index pairs into the seventeen keypoints. Face, arms,
/// torso and legs in that order, so that a partial skeleton still reads as a
/// person rather than as a scatter of lines.
const QVariantList &cocoSkeleton()
{
    static const QVariantList edges{
        QVariantList{0, 1},   QVariantList{0, 2},   QVariantList{1, 3},
        QVariantList{2, 4},   QVariantList{5, 6},   QVariantList{5, 7},
        QVariantList{7, 9},   QVariantList{6, 8},   QVariantList{8, 10},
        QVariantList{5, 11},  QVariantList{6, 12},  QVariantList{11, 12},
        QVariantList{11, 13}, QVariantList{13, 15}, QVariantList{12, 14},
        QVariantList{14, 16}, QVariantList{0, 5},   QVariantList{0, 6},
    };
    return edges;
}

/// The MediaPipe hand bones, as index pairs into the twenty-one points. The
/// palm first, then the five digits from the thumb outwards, so that a hand
/// which only half decoded still reads as a hand rather than as a spray of
/// segments.
const QVariantList &mediapipeHand()
{
    static const QVariantList edges{
        QVariantList{0, 1},   QVariantList{0, 5},   QVariantList{5, 9},
        QVariantList{9, 13},  QVariantList{13, 17}, QVariantList{0, 17},
        QVariantList{1, 2},   QVariantList{2, 3},   QVariantList{3, 4},
        QVariantList{5, 6},   QVariantList{6, 7},   QVariantList{7, 8},
        QVariantList{9, 10},  QVariantList{10, 11}, QVariantList{11, 12},
        QVariantList{13, 14}, QVariantList{14, 15}, QVariantList{15, 16},
        QVariantList{17, 18}, QVariantList{18, 19}, QVariantList{19, 20},
    };
    return edges;
}

QVariantList landmarksToVariant(const QList<QPointF> &landmarks)
{
    QVariantList list;
    list.reserve(landmarks.size());
    for (const QPointF &point : landmarks) {
        list.append(QVariantMap{{QStringLiteral("x"), point.x()},
                                {QStringLiteral("y"), point.y()}});
    }
    return list;
}

QVariantMap faceToVariant(const Kerfur::AnalysedFace &face)
{
    return QVariantMap{
        {QStringLiteral("box"), face.box},
        {QStringLiteral("landmarks"), landmarksToVariant(face.landmarks)},
        {QStringLiteral("identity"), face.identity},
        {QStringLiteral("identityConfidence"), face.identityConfidence},
        {QStringLiteral("known"), !face.identity.isEmpty()},
        {QStringLiteral("emotion"), face.emotion},
        {QStringLiteral("emotionConfidence"), face.emotionConfidence},
        {QStringLiteral("yaw"), face.pose.yaw},
        {QStringLiteral("pitch"), face.pose.pitch},
        {QStringLiteral("roll"), face.pose.roll},
        {QStringLiteral("poseValid"), face.pose.valid},
        {QStringLiteral("gaze"), face.gazeDirection},
        {QStringLiteral("priority"), face.priority},
        {QStringLiteral("ranked"), face.priority != Kerfur::IdentityRegistry::unknownPriority},
        // What the manifest calls this person. The overlay writes this and not
        // the rank beside it: the rank is for sorting, and a number under a
        // chin means nothing to whoever is reading the screen.
        {QStringLiteral("title"), face.title},
    };
}

QVariantMap handToVariant(const Kerfur::AnalysedHand &hand)
{
    QVariantList landmarks;
    landmarks.reserve(hand.landmarks.size());
    for (const QPointF &point : hand.landmarks) {
        landmarks.append(point);
    }
    return QVariantMap{{QStringLiteral("box"), hand.box},
                       {QStringLiteral("wrist"), hand.wrist},
                       {QStringLiteral("side"), hand.side},
                       {QStringLiteral("confidence"), hand.confidence},
                       // Empty unless the landmark models are provisioned and
                       // this hand was one of the ones the frame's budget
                       // reached, so a view must check `tracked` and not the
                       // emptiness of this list, which is also empty when the
                       // model simply failed to find a palm.
                       {QStringLiteral("landmarks"), landmarks},
                       {QStringLiteral("grip"), hand.grip},
                       {QStringLiteral("gesture"), hand.gesture},
                       // Minus one for a hand that was never measured, so a
                       // view has to test for that before printing a count.
                       {QStringLiteral("fingers"), hand.fingers},
                       {QStringLiteral("tracked"), hand.tracked}};
}

QVariantMap heldToVariant(const Kerfur::HeldObject &object)
{
    return QVariantMap{{QStringLiteral("box"), object.box},
                       {QStringLiteral("label"), object.label},
                       {QStringLiteral("score"), object.score},
                       {QStringLiteral("side"), object.side}};
}

QVariantList handsOf(const Kerfur::AnalysedPerson &person)
{
    QVariantList hands;
    if (person.leftHand.valid) {
        hands.append(handToVariant(person.leftHand));
    }
    if (person.rightHand.valid) {
        hands.append(handToVariant(person.rightHand));
    }
    return hands;
}

QVariantList heldOf(const Kerfur::AnalysedPerson &person)
{
    QVariantList held;
    held.reserve(person.held.size());
    for (const Kerfur::HeldObject &object : person.held) {
        held.append(heldToVariant(object));
    }
    return held;
}

QVariantMap personToVariant(const Kerfur::AnalysedPerson &person)
{
    QVariantList keypoints;
    keypoints.reserve(person.keypoints.size());
    for (qsizetype index{0}; index < person.keypoints.size(); ++index) {
        keypoints.append(QVariantMap{
            {QStringLiteral("x"), person.keypoints.at(index).x()},
            {QStringLiteral("y"), person.keypoints.at(index).y()},
            {QStringLiteral("score"),
             index < person.keypointScores.size() ? person.keypointScores.at(index) : 0.0},
        });
    }
    return QVariantMap{{QStringLiteral("box"), person.box},
                       {QStringLiteral("score"), person.score},
                       {QStringLiteral("keypoints"), keypoints},
                       {QStringLiteral("identity"), person.identity},
                       {QStringLiteral("identityConfidence"), person.identityConfidence},
                       {QStringLiteral("priority"), person.priority},
                       {QStringLiteral("title"), person.title},
                       {QStringLiteral("known"), person.known},
                       {QStringLiteral("hands"), handsOf(person)},
                       {QStringLiteral("held"), heldOf(person)}};
}

QString describeSize(const QSize &size)
{
    if (!size.isValid() || size.isEmpty()) {
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

} // namespace

FaceTracker::FaceTracker(QObject *parent)
    : QObject{parent}
    , m_camera{this}
    , m_captureSession{this}
    , m_videoSink{this}
    , m_libcamera{this}
    , m_frameThrottleTimer{this}
    , m_cameraRetryTimer{this}
    , m_startWatchdog{this}
{
    Kerfur::ensureFaceAnalysisMetaTypesRegistered();

    m_clock.start();
    applyIdleLimits();
    m_frameThrottleTimer.setSingleShot(true);
    m_frameThrottleTimer.setInterval(frameInterval());

    m_cameraRetryTimer.setSingleShot(true);
    m_cameraRetryTimer.setInterval(2000);
    connect(&m_cameraRetryTimer, &QTimer::timeout, this, &FaceTracker::retryCameraInitialization);

    m_startWatchdog.setSingleShot(true);
    m_startWatchdog.setInterval(5000);
    connect(&m_startWatchdog, &QTimer::timeout, this, &FaceTracker::checkCameraStarted);

    connect(&m_mediaDevices,
            &QMediaDevices::videoInputsChanged,
            this,
            &FaceTracker::handleVideoInputsChanged);

    connect(&m_libcamera,
            &Kerfur::LibcameraCapture::frameReady,
            this,
            &FaceTracker::processLibcameraFrame);
    connect(&m_libcamera, &Kerfur::LibcameraCapture::started, this, [this]() {
        setErrorString(QString{});
        m_cameraRetryTimer.stop();
        emit currentResolutionChanged();
    });
    connect(&m_libcamera,
            &Kerfur::LibcameraCapture::errorOccurred,
            this,
            [this](const QString &message) {
                setErrorString(QStringLiteral("Camera error: %1").arg(message));
                if (m_enabled && !m_cameraRetryTimer.isActive()) {
                    m_cameraRetryTimer.start();
                }
            });

    // A light session loads no model, so it has no worker to load them in.
    if (!Kerfur::Session::light()) {
        startWorker();
    }
    enumerateCameras();
}

FaceTracker::~FaceTracker()
{
    // Before the thread goes away, so that a pass in flight cannot land on a
    // half destroyed tracker.
    disconnect(&m_videoSink, nullptr, this, nullptr);
    m_libcamera.stop();
    m_workerThread.quit();
    m_workerThread.wait();
}

void FaceTracker::startWorker()
{
    m_worker = new Kerfur::FaceAnalysisWorker;
    m_worker->moveToThread(&m_workerThread);

    // Deleted by the thread that owns it, which is the only thread allowed to.
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this,
            &FaceTracker::analysisRequested,
            m_worker,
            &Kerfur::FaceAnalysisWorker::analyse);

    connect(m_worker,
            &Kerfur::FaceAnalysisWorker::resultReady,
            this,
            &FaceTracker::applyResult);
    connect(m_worker, &Kerfur::FaceAnalysisWorker::ready, this, &FaceTracker::applyCapabilities);
    // Queued onto the worker's thread, after the initialize below.
    connect(&Kerfur::IdentityLibrary::instance(),
            &Kerfur::IdentityLibrary::galleryChanged,
            m_worker,
            &Kerfur::FaceAnalysisWorker::reloadGallery);

    // The worker starts from this object's thresholds, so the two never
    // disagree about a value nobody has set yet. Called directly: its thread
    // has not started, so nothing else can be touching it.
    const std::array<std::pair<Kerfur::VisionThreshold, qreal>, 7> thresholds{{
        {Kerfur::VisionThreshold::Face, m_detectionConfidence},
        {Kerfur::VisionThreshold::Person, m_personConfidence},
        {Kerfur::VisionThreshold::Keypoint, m_keypointConfidence},
        {Kerfur::VisionThreshold::Identity, m_identityThreshold},
        {Kerfur::VisionThreshold::Palm, m_palmConfidence},
        {Kerfur::VisionThreshold::Hand, m_handConfidence},
        {Kerfur::VisionThreshold::Object, m_objectConfidence},
    }};
    for (const auto &[which, value] : thresholds) {
        m_worker->setThreshold(which, value);
    }

    m_workerThread.setObjectName(QStringLiteral("kerfur-vision"));
    m_workerThread.start();

    // Loading four models and embedding the gallery takes seconds. Queued, so
    // that none of it happens before the application has finished starting.
    QMetaObject::invokeMethod(m_worker,
                              &Kerfur::FaceAnalysisWorker::initialize,
                              Qt::QueuedConnection);
}

bool FaceTracker::enumerateCameras()
{
    if (!m_libcamerasProbed && !Kerfur::Session::light()) {
        m_libcameras = Kerfur::libcameraDevices();
        m_libcamerasProbed = true;
    }

    const QList<QCameraDevice> cameras{QMediaDevices::videoInputs()};
    QList<Kerfur::CameraCandidate> candidates;
    candidates.reserve(cameras.size() + m_libcameras.size());
    for (const QCameraDevice &camera : cameras) {
        candidates.append(Kerfur::CameraCandidate{camera.id(),
                                                  camera.description(),
                                                  camera.position()
                                                      == QCameraDevice::FrontFace});
    }
    for (const Kerfur::LibcameraDevice &camera : std::as_const(m_libcameras)) {
        candidates.append(Kerfur::CameraCandidate{Kerfur::libcameraCameraId(camera.name),
                                                  camera.model,
                                                  false});
    }

    const QByteArray previous{preferredCameraId()};
    const qsizetype chosen{Kerfur::chooseCamera(candidates,
                                                m_wantedCamera,
                                                previous,
                                                QMediaDevices::defaultVideoInput().id())};
    if (chosen >= 0 && chosen < cameras.size()) {
        m_preferredCamera = cameras.at(chosen);
        m_preferredLibcamera = -1;
    } else {
        m_preferredCamera = QCameraDevice{};
        m_preferredLibcamera = chosen >= 0 ? chosen - cameras.size() : -1;
    }

    const QStringList labels{Kerfur::cameraLabels(candidates)};
    const bool listChanged{cameras != m_cameras || labels != m_cameraLabels};
    m_cameras = cameras;
    m_cameraLabels = labels;

    const bool switched{preferredCameraId() != previous};
    if (listChanged || switched) {
        emit availableCamerasChanged();
    }
    // An automatic field of view is the chosen camera's.
    if (switched) {
        emit mountChanged();
    }
    // The resolutions are the chosen device's, and the browser backend only
    // has a device to choose some time after start up.
    if (switched) {
        emit availableResolutionsChanged();
    }
    return switched;
}

void FaceTracker::handleVideoInputsChanged()
{
    const QByteArray running{m_openCameraId};
    enumerateCameras();
    qCInfo(trackerLog) << m_cameras.size() + m_libcameras.size() << "camera(s) available";

    // A running camera keeps its stream even when the entry it was opened from
    // is replaced, which the browser does once permission is granted. The one
    // exception is the chosen camera coming back while a fallback is open.
    if (m_enabled && !m_isInitializing && !cameraActive()) {
        initializeCamera();
    } else if (m_enabled && cameraActive() && !m_wantedCamera.isEmpty()
               && preferredCameraId() == m_wantedCamera && running != m_wantedCamera) {
        qCInfo(trackerLog) << "chosen camera" << currentCamera() << "is back";
        restartCamera();
    }
}

QByteArray FaceTracker::preferredCameraId() const
{
    if (usingLibcamera()) {
        return Kerfur::libcameraCameraId(m_libcameras.at(m_preferredLibcamera).name);
    }
    return m_preferredCamera.id();
}

QByteArray FaceTracker::cameraIdAt(qsizetype index) const
{
    if (index >= 0 && index < m_cameras.size()) {
        return m_cameras.at(index).id();
    }
    const qsizetype libcameraIndex{index - m_cameras.size()};
    if (libcameraIndex >= 0 && libcameraIndex < m_libcameras.size()) {
        return Kerfur::libcameraCameraId(m_libcameras.at(libcameraIndex).name);
    }
    return {};
}

bool FaceTracker::cameraActive() const
{
    return m_libcamera.isRunning() || m_camera.isActive();
}

QString FaceTracker::currentCamera() const
{
    const qsizetype index{usingLibcamera() ? m_cameras.size() + m_preferredLibcamera
                                           : m_cameras.indexOf(m_preferredCamera)};
    return index >= 0 && index < m_cameraLabels.size() ? m_cameraLabels.at(index) : QString{};
}

QString FaceTracker::cameraIdFor(const QString &label) const
{
    return QString::fromUtf8(cameraIdAt(m_cameraLabels.indexOf(label)));
}

void FaceTracker::setCameraId(const QString &id)
{
    const QByteArray wanted{id.toUtf8()};
    if (m_wantedCamera == wanted) {
        return;
    }
    m_wantedCamera = wanted;
    emit cameraIdChanged();

    if (enumerateCameras() && m_enabled) {
        qCInfo(trackerLog) << "switching to camera" << currentCamera();
        restartCamera();
    }
}

void FaceTracker::restartCamera()
{
    // initializeCamera() refuses while an open is pending; the pending open's
    // handlers are disconnected by uninitializeCamera(), so dropping the flag
    // here cannot let a stale activeChanged land on the new device.
    m_isInitializing = false;
    initializeCamera();
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    // The setting is kept as it is, so the next full session looks again.
    if (enabled && Kerfur::Session::light()) {
        return;
    }

    m_enabled = enabled;

    if (m_enabled) {
        stopWatching(false);
        initializeCamera();
    } else {
        m_hasLastKnownPosition = false;
        m_hasGaze = false;
        uninitializeCamera();
    }

    emit enabledChanged();
}

void FaceTracker::selectCameraFormat()
{
    const QList<QCameraFormat> formats{m_preferredCamera.videoFormats()};
    if (formats.isEmpty()) {
        return;
    }

    // Exact first, then the smallest mode that still covers the request, then
    // simply the largest there is. Ties are broken on frame rate, and the rule
    // is the slowest mode that still comfortably covers the cadence -- not the
    // fastest available.
    //
    // Faster is not better here. Nothing downstream consumes more than
    // `m_updateHz` frames a second; a sixty hertz mode means the driver
    // decoding sixty frames, the backend converting sixty frames and this class
    // discarding fifty of them, at twice the bus bandwidth and twice the power
    // of the thirty hertz mode that produces exactly the same preview.
    const auto better = [](const QCameraFormat &candidate, const QCameraFormat &incumbent) {
        const bool candidateCovers{candidate.maxFrameRate() >= preferredCaptureFrameRate};
        const bool incumbentCovers{incumbent.maxFrameRate() >= preferredCaptureFrameRate};
        if (candidateCovers != incumbentCovers) {
            return candidateCovers;
        }
        // Both cover it: the slower one. Neither does: the faster one, which is
        // the closest either can get to the cadence being asked for.
        return candidateCovers ? candidate.maxFrameRate() < incumbent.maxFrameRate()
                               : candidate.maxFrameRate() > incumbent.maxFrameRate();
    };

    QCameraFormat exact;
    QCameraFormat covering;
    QCameraFormat largest;

    for (const QCameraFormat &format : formats) {
        const QSize resolution{format.resolution()};
        if (resolution == m_requestedResolution
            && (exact.isNull() || better(format, exact))) {
            exact = format;
        }
        if (resolution.width() >= m_requestedResolution.width()
            && resolution.height() >= m_requestedResolution.height()) {
            const bool smaller{covering.isNull()
                               || resolution.width() * resolution.height()
                                      < covering.resolution().width()
                                            * covering.resolution().height()};
            if (smaller || (resolution == covering.resolution() && better(format, covering))) {
                covering = format;
            }
        }
        const bool bigger{largest.isNull()
                          || resolution.width() * resolution.height()
                                 > largest.resolution().width() * largest.resolution().height()};
        if (bigger || (resolution == largest.resolution() && better(format, largest))) {
            largest = format;
        }
    }

    const QCameraFormat chosen{!exact.isNull() ? exact : (!covering.isNull() ? covering : largest)};
    if (chosen.isNull()) {
        return;
    }

    m_camera.setCameraFormat(chosen);
    qCInfo(trackerLog) << "requested" << m_requestedResolution << "chose format"
                       << chosen.resolution() << "at" << chosen.maxFrameRate() << "fps";
}

void FaceTracker::initializeCamera()
{
    if (m_isInitializing) {
        return;
    }

    const auto retry{[this]() {
        if (m_enabled) {
            initializeCamera();
        }
    }};
    switch (m_cameraPermission.ensure(this, retry)) {
    case Kerfur::CapturePermission::Access::Allowed:
        break;
    case Kerfur::CapturePermission::Access::Pending:
        return;
    case Kerfur::CapturePermission::Access::Denied:
        setErrorString(QStringLiteral("Camera access was denied"));
        return;
    }

    m_isInitializing = true;

    if (!hasPreferredCamera()) {
        enumerateCameras();
    }
    if (!hasPreferredCamera()) {
        setErrorString(QStringLiteral("No cameras available"));
        m_isInitializing = false;
        return;
    }

    uninitializeCamera();

    if (usingLibcamera()) {
        startLibcamera();
        return;
    }

    m_openCameraId = m_preferredCamera.id();
    m_camera.setCameraDevice(m_preferredCamera);
    selectCameraFormat();

    connect(&m_camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error, const QString &errorString) {
                setErrorString(QStringLiteral("Camera error: %1").arg(errorString));
                if (m_enabled && !m_cameraRetryTimer.isActive()) {
                    m_cameraRetryTimer.start();
                }
            });

    connect(&m_camera, &QCamera::activeChanged, this, [this](bool active) {
        if (active) {
            // Every error this property carries is about the camera, and the
            // camera is now running.
            setErrorString(QString{});
            m_cameraRetryTimer.stop();
            m_isInitializing = false;
            emit currentResolutionChanged();
        } else if (m_isInitializing) {
            m_isInitializing = false;
            setErrorString(QStringLiteral("Camera failed to activate"));
        }
    });

    connect(&m_videoSink, &QVideoSink::videoFrameChanged, this, &FaceTracker::processVideoFrame);

    m_captureSession.setCamera(&m_camera);
    m_captureSession.setVideoSink(&m_videoSink);
    m_camera.start();
    m_startWatchdog.start();
}

void FaceTracker::startLibcamera()
{
    const Kerfur::LibcameraDevice &device{m_libcameras.at(m_preferredLibcamera)};
    m_openCameraId = Kerfur::libcameraCameraId(device.name);
    // Starting is synchronous here: what remains asynchronous is the first
    // frame, and the watchdog and `started` cover that.
    m_isInitializing = false;
    if (!m_libcamera.start(device, m_requestedResolution)) {
        setErrorString(QStringLiteral("Camera error: %1").arg(m_libcamera.errorString()));
        if (m_enabled && !m_cameraRetryTimer.isActive()) {
            m_cameraRetryTimer.start();
        }
        return;
    }
    m_startWatchdog.start();
}

void FaceTracker::checkCameraStarted()
{
    if (!m_enabled) {
        return;
    }
    if (!cameraActive()) {
        setErrorString(QStringLiteral("Camera failed to start within 5 seconds"));
        m_isInitializing = false;
        return;
    }
    // The browser backend reports the camera active before its stream has
    // produced a frame, and keeps reporting it when the stream never does.
    if (m_captureResolution.isEmpty() && m_errorString.isEmpty()) {
        setErrorString(QStringLiteral("Camera is running but no picture has arrived"));
    }
}

void FaceTracker::retryCameraInitialization()
{
    if (m_enabled && !cameraActive()) {
        qCInfo(trackerLog) << "retrying camera initialization";
        enumerateCameras();
        initializeCamera();
    }
}

void FaceTracker::uninitializeCamera()
{
    m_frameThrottleTimer.stop();
    m_cameraRetryTimer.stop();
    m_startWatchdog.stop();

    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged, this, nullptr);
    disconnect(&m_camera, &QCamera::errorOccurred, this, nullptr);
    disconnect(&m_camera, &QCamera::activeChanged, this, nullptr);

    if (m_camera.isActive()) {
        m_camera.stop();
    }
    m_libcamera.stop();
    m_openCameraId.clear();
    stopWatching(false);

    setFaceDetected(false);
    m_pendingPreview = QImage{};
    setCameraFrame(QImage{});
    setCaptureResolution(QSize{});

    if (!m_faces.isEmpty() || !m_people.isEmpty() || !m_hands.isEmpty()
        || !m_held.isEmpty()) {
        m_faces.clear();
        m_faceLandmarks.clear();
        m_primaryFace.clear();
        m_people.clear();
        m_hands.clear();
        m_held.clear();
        emit facesChanged();
        emit peopleChanged();
    }
}

bool FaceTracker::acceptFrame()
{
    if (!m_enabled) {
        return false;
    }

    if (m_frameThrottleTimer.isActive()) {
        return false;
    }

    // One pass at a time, and the throttle is not restarted for a frame that
    // cannot start one. Queueing a second pass while the first is still running
    // would build a backlog of images that are already out of date by the time
    // a model reaches them, which is the failure that turns a slow preview into
    // an unbounded one; skipping the throttle restart means the next frame to
    // arrive after the pass finishes is used, rather than the one after that.
    //
    // While the models are still loading there is no pass to wait for and the
    // first request sits queued behind the load, so frames are shown as they
    // arrive instead. Loading four networks and a gallery takes seconds, and a
    // panel that shows nothing at all for those seconds looks like a camera
    // that failed rather than like a robot waking up.
    if (m_analysisBusy && m_analysisReady) {
        return false;
    }
    m_frameThrottleTimer.start();
    return true;
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!acceptFrame()) {
        return;
    }

    const QImage captured{Kerfur::imageFromVideoFrame(frame)};
    if (watching() && !captured.isNull()) {
        if (!motionWakes(Kerfur::MotionGate::thumbnail(captured))) {
            showWatchedFrame(captured);
            return;
        }
        stopWatching(true);
    }
    if (captured.isNull()) {
        const Kerfur::PlaneFill fill{Kerfur::firstPlaneFill(frame)};
        if (fill.held < fill.needed) {
            setErrorString(QStringLiteral("Camera frames are incomplete: %1 KB of the %2 KB a %3 "
                                          "%4x%5 frame needs; try another mode or camera")
                               .arg(fill.held / 1024)
                               .arg(fill.needed / 1024)
                               .arg(QVideoFrameFormat::pixelFormatToString(frame.pixelFormat()))
                               .arg(frame.width())
                               .arg(frame.height()));
        } else {
            setErrorString(QStringLiteral("Camera frames arrive as %1 %2x%3 and cannot be read")
                               .arg(QVideoFrameFormat::pixelFormatToString(frame.pixelFormat()))
                               .arg(frame.width())
                               .arg(frame.height()));
        }
        return;
    }
    processCapturedImage(captured);
}

void FaceTracker::processLibcameraFrame()
{
    if (!acceptFrame()) {
        return;
    }
    // Watching reads the luma plane of the frame and nothing else. The colour
    // conversion is paid only for a frame that wakes the models, or one a view
    // is showing.
    if (watching()) {
        if (!m_libcamera.holdLatest()) {
            return;
        }
        const bool wakes{motionWakes(m_libcamera.heldLumaThumbnail())};
        if (!wakes && m_previewViewers == 0) {
            m_libcamera.releaseHeld();
            return;
        }
        const QImage held{m_libcamera.heldFrame()};
        m_libcamera.releaseHeld();
        if (!wakes) {
            showWatchedFrame(held);
            return;
        }
        stopWatching(true);
        if (held.isNull()) {
            return;
        }
        processCapturedImage(held);
        return;
    }
    const QImage captured{m_libcamera.takeFrame()};
    if (captured.isNull()) {
        // Also the case of a notification that found the frame already taken,
        // which says nothing; a frame that could not be read says why.
        if (!m_libcamera.errorString().isEmpty()) {
            setErrorString(m_libcamera.errorString());
        }
        return;
    }
    processCapturedImage(captured);
}

void FaceTracker::processCapturedImage(const QImage &captured)
{
    // A picture is the end of every error this class reports about the
    // camera, whichever of them was standing.
    setErrorString(QString{});

    // The truth about the resolution, taken from a frame that actually arrived
    // rather than from the format that was asked for.
    setCaptureResolution(captured.size());

    // Mirrored, and everything downstream works in the mirrored frame: the
    // preview, the overlay geometry and `faceCenter`, which is what makes the
    // rendered eyes follow the viewer the way a mirror would. The one place the
    // reflection has to be undone is the head pose solve, and the worker is
    // told that it is looking at one.
    const QImage image{captured.flipped(Qt::Horizontal)};

    // Reduced, never enlarged, exactly like the analysis image below. A preview
    // scaled up past the frame that arrived is a blurrier picture of the same
    // pixels for the texture upload to carry.
    QSize previewSize{image.size()};
    if (std::max(image.width(), image.height()) > m_previewMaxSide) {
        previewSize.scale(m_previewMaxSide, m_previewMaxSide, Qt::KeepAspectRatio);
    }
    const QImage preview{
        previewSize == image.size()
            ? image
            : image.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)};

    // A worker that loaded nothing has no measurement to hold the picture
    // for, so the preview runs at the capture cadence on its own.
    const bool analysing{m_worker != nullptr && (!m_analysisReady || canAnalyse())};
    if (!m_analysisReady || !analysing) {
        setCameraFrame(preview);
    }
    if (m_analysisBusy || !analysing) {
        return;
    }

    // Reduced, never enlarged. Asking for a longer side than the camera
    // delivers would interpolate pixels that were never measured: it costs a
    // full resample and a bigger buffer through every stage, and gives the
    // models nothing they did not already have.
    QSize analysisSize{image.size()};
    if (std::max(image.width(), image.height()) > m_analysisMaxSide) {
        analysisSize.scale(m_analysisMaxSide, m_analysisMaxSide, Qt::KeepAspectRatio);
    }
    const QImage analysisImage{analysisSize == image.size()
                                   ? image
                                   : image.scaled(analysisSize,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation)};

    // The picture is held back until the pass measured against it comes home.
    // Showing it now would be a hundred milliseconds ahead of every box drawn
    // on it, and an overlay that trails the picture underneath it reads as a
    // model that cannot find a hand rather than as a frame out of order.
    const quint64 sequence{++m_sequence};
    m_pendingPreview = preview;
    m_pendingPreviewSequence = sequence;
    m_analysisBusy = true;
    emit analysisRequested(analysisImage, sequence, true);
}

void FaceTracker::applyCapabilities(const Kerfur::AnalysisCapabilities &capabilities)
{
    m_capabilities = capabilities;
    // The worker is now answering, so from here a picture waits for the pass
    // measured on it whenever a model loaded to measure it with.
    m_analysisReady = true;
    emit capabilitiesChanged();

    // A pass queued behind the load comes back empty, and the picture it holds
    // is older than the ones the preview is about to show directly.
    if (!canAnalyse()) {
        m_pendingPreview = QImage{};
    }

    // Published as `perceptionProblem`, never as the error string: the camera
    // and the preview work the same with no model at all.
    if (!capabilities.problem.isEmpty()) {
        qCWarning(trackerLog) << capabilities.problem;
    } else {
        qCInfo(trackerLog) << "analysis ready; gallery" << capabilities.galleryDirectory
                           << "holds" << capabilities.enrolled;
    }
}

bool FaceTracker::canAnalyse() const
{
    return m_capabilities.faceReady || m_capabilities.poseReady;
}

void FaceTracker::applyResult(const Kerfur::AnalysisResult &result)
{
    m_analysisBusy = false;

    if (m_analysisSize != result.frameSize || m_analysisMs != result.elapsedMs) {
        m_analysisSize = result.frameSize;
        m_analysisMs = result.elapsedMs;
        emit analysisChanged();
    }

    if (!m_enabled) {
        return;
    }

    notePresence(!result.faces.isEmpty() || !result.people.isEmpty());

    // The picture this pass was measured against, now that the measurements
    // are here to be drawn on it.
    if (result.sequence == m_pendingPreviewSequence && !m_pendingPreview.isNull()) {
        setCameraFrame(m_pendingPreview);
        m_pendingPreview = QImage{};
    }

    if (m_peopleRevision != result.peopleRevision) {
        m_peopleRevision = result.peopleRevision;
        QVariantList people;
        people.reserve(result.people.size());
        for (const Kerfur::AnalysedPerson &person : result.people) {
            people.append(personToVariant(person));
        }
        m_people = people;
        emit peopleChanged();
    }

    // Rank first, size second. Kerfur looks at the highest ranked person in the
    // room, and only falls back to the nearest one among equals -- which, since
    // every stranger shares the same unknown rank, is what a room full of
    // strangers reduces to. Sorting the other way round was the old behaviour
    // and it meant whoever leaned closest to the camera won.
    QList<Kerfur::AnalysedFace> ordered{result.faces};
    std::sort(ordered.begin(),
              ordered.end(),
              [](const Kerfur::AnalysedFace &left, const Kerfur::AnalysedFace &right) {
                  if (left.priority != right.priority) {
                      return left.priority < right.priority;
                  }
                  return left.box.width() * left.box.height()
                         > right.box.width() * right.box.height();
              });

    const Kerfur::SceneGeometry geometry{sceneGeometry(result.frameSize)};
    const QList<Kerfur::Placement> placements{placeFaces(geometry, ordered, result.people)};

    QVariantList faces;
    faces.reserve(ordered.size());
    for (qsizetype index{0}; index < ordered.size(); ++index) {
        QVariantMap face{faceToVariant(ordered.at(index))};
        const Kerfur::Placement &placement{placements.at(index)};
        face.insert(QStringLiteral("distance"),
                    placement.isValid() ? placement.floorDistanceMetres : -1.0);
        face.insert(QStringLiteral("distanceSource"), Kerfur::rangeSourceName(placement.source));
        faces.append(face);
    }
    m_faces = faces;
    m_attentionDistance = placements.isEmpty() || !placements.first().isValid()
                              ? -1.0
                              : placements.first().floorDistanceMetres;

    if (ordered.isEmpty()) {
        m_primaryFace.clear();
        m_faceLandmarks.clear();
        emit facesChanged();
        setAttention({}, {});

        setFaceDetected(false);
        if (!m_hasLastKnownPosition) {
            setFaceCenter(QPointF{0.5, 0.5});
        }
        return;
    }

    const Kerfur::AnalysedFace &primary{ordered.first()};
    m_primaryFace = faces.first().toMap();
    m_faceLandmarks = landmarksToVariant(primary.landmarks);
    emit facesChanged();

    // The body that goes with that face. By name when there is one, because a
    // name is the only association that survives two people crossing; by
    // geometry otherwise, which is all there is for a stranger. A stranger has
    // hands to publish only while they are alone in view.
    const Kerfur::AnalysedPerson *attention{nullptr};
    for (const Kerfur::AnalysedPerson &person : result.people) {
        if (!primary.identity.isEmpty() && person.identity == primary.identity) {
            attention = &person;
            break;
        }
        if (attention == nullptr && person.box.contains(primary.box.center())) {
            attention = &person;
        }
    }
    if (attention == nullptr) {
        setAttention({}, {});
    } else {
        setAttention(handsOf(*attention), heldOf(*attention));
    }

    const QPointF centre{primary.box.center()};
    const qreal smoothing{std::clamp(m_trackingSmoothing, 0.0, 1.0)};
    if (m_hasLastKnownPosition && smoothing > 0.0) {
        setFaceCenter(QPointF{m_faceCenter.x() * smoothing + centre.x() * (1.0 - smoothing),
                              m_faceCenter.y() * smoothing + centre.y() * (1.0 - smoothing)});
    } else {
        setFaceCenter(centre);
    }
    m_hasLastKnownPosition = true;

    // Aimed at the face itself when it could be placed, so that somebody close
    // to the screen is looked at from where the eyes are drawn rather than
    // from the lens. A face that could not be placed is far enough away, or
    // unknown enough, for the direction of its ray to be all there is.
    //
    // Detected first: a view that follows the gaze only while a face is
    // detected must not miss the first one.
    setFaceDetected(true);
    const Kerfur::Placement &placement{placements.first()};
    setGaze(placement.isValid() ? geometry.gazeAt(placement.position)
                                : geometry.gazeAlong(geometry.ray(centre)));
}

Kerfur::SceneGeometry FaceTracker::sceneGeometry(const QSize &frame) const
{
    Kerfur::CameraMount mount{m_mount};
    mount.horizontalFovDegrees = effectiveFieldOfView();
    return Kerfur::SceneGeometry{mount, frame};
}

QList<Kerfur::Placement> FaceTracker::placeFaces(const Kerfur::SceneGeometry &geometry,
                                                 const QList<Kerfur::AnalysedFace> &faces,
                                                 const QList<Kerfur::AnalysedPerson> &people) const
{
    // Ankles the pose model is at least this sure of stand on the floor.
    constexpr double ankleConfidence{0.5};
    const auto feetOf = [](const Kerfur::AnalysedPerson &person) -> std::optional<QPointF> {
        QPointF sum;
        int seen{0};
        for (const Kerfur::Keypoint ankle : {Kerfur::Keypoint::LeftAnkle,
                                             Kerfur::Keypoint::RightAnkle}) {
            const auto index{static_cast<qsizetype>(ankle)};
            if (index < person.keypoints.size() && index < person.keypointScores.size()
                && person.keypointScores.at(index) >= ankleConfidence) {
                sum += person.keypoints.at(index);
                ++seen;
            }
        }
        if (seen == 0) {
            return std::nullopt;
        }
        return sum / seen;
    };

    QList<Kerfur::Placement> placements;
    placements.reserve(faces.size());
    for (const Kerfur::AnalysedFace &face : faces) {
        Kerfur::FaceSighting sighting;
        sighting.centre = face.box.center();
        if (face.landmarks.size() >= 2) {
            sighting.rightEye = face.landmarks.at(0);
            sighting.leftEye = face.landmarks.at(1);
        }
        sighting.yawDegrees = face.pose.valid ? face.pose.yaw : 0.0;
        sighting.heightMetres = face.heightMetres;

        const Kerfur::AnalysedPerson *body{nullptr};
        for (const Kerfur::AnalysedPerson &person : people) {
            if (!face.identity.isEmpty() && person.identity == face.identity) {
                body = &person;
                break;
            }
            if (body == nullptr && person.box.contains(sighting.centre)) {
                body = &person;
            }
        }
        if (body != nullptr) {
            sighting.feet = feetOf(*body);
        }
        placements.append(geometry.place(sighting));
    }
    return placements;
}

void FaceTracker::setGaze(const Kerfur::GazeAngles &gaze)
{
    Kerfur::GazeAngles next{gaze};
    const qreal smoothing{std::clamp(m_trackingSmoothing, 0.0, 1.0)};
    if (m_hasGaze && smoothing > 0.0) {
        next.yawDegrees = m_gaze.yawDegrees * smoothing + gaze.yawDegrees * (1.0 - smoothing);
        next.pitchDegrees = m_gaze.pitchDegrees * smoothing
                            + gaze.pitchDegrees * (1.0 - smoothing);
    }
    m_hasGaze = true;
    if (qFuzzyCompare(next.yawDegrees, m_gaze.yawDegrees)
        && qFuzzyCompare(next.pitchDegrees, m_gaze.pitchDegrees)) {
        return;
    }
    m_gaze = next;
    emit gazeChanged();
}

qreal FaceTracker::effectiveFieldOfView() const
{
    if (m_fieldOfView > 0.0) {
        return m_fieldOfView;
    }
    if (usingLibcamera() && m_preferredLibcamera < m_libcameras.size()) {
        const double known{
            Kerfur::fieldOfViewForSensor(m_libcameras.at(m_preferredLibcamera).model)};
        if (known > 0.0) {
            return known;
        }
    }
    return fallbackFieldOfView;
}

int FaceTracker::frameInterval() const
{
    if (watching()) {
        return static_cast<int>(std::lround(1000.0 / m_watchHz));
    }
    return intervalForRate(m_updateHz);
}

void FaceTracker::applyIdleLimits()
{
    m_idleClock.setLimits(static_cast<qint64>(m_idleAfterSeconds) * 1000,
                          static_cast<qint64>(std::lround(m_probeSeconds * 1000.0)));
}

void FaceTracker::notePresence(bool present)
{
    if (m_idleClock.notePass(present, m_clock.elapsed())) {
        startWatching();
    }
}

void FaceTracker::startWatching()
{
    // The first watched frame becomes the reference, so a change is measured
    // against the empty room as it is now.
    m_motionGate.clear();
    m_pendingPreview = QImage{};
    m_frameThrottleTimer.setInterval(frameInterval());
    qCInfo(trackerLog) << "nobody in view; watching for motion at" << m_watchHz << "Hz";
    emit watchingChanged();
}

void FaceTracker::stopWatching(bool probe)
{
    const bool wasWatching{watching()};
    if (probe) {
        m_idleClock.wake(m_clock.elapsed());
    } else {
        m_idleClock.reset(m_clock.elapsed());
    }
    if (!wasWatching) {
        return;
    }
    m_frameThrottleTimer.setInterval(frameInterval());
    if (probe) {
        qCInfo(trackerLog) << "motion" << m_motionPercent << "%; looking for somebody";
    }
    emit watchingChanged();
}

bool FaceTracker::motionWakes(const QImage &thumbnail)
{
    if (thumbnail.isNull()) {
        return false;
    }
    if (!m_motionGate.hasReference()) {
        m_motionGate.setReference(thumbnail);
        return false;
    }
    const qreal percent{m_motionGate.changedFraction(thumbnail) * 100.0};
    if (!qFuzzyCompare(percent + 1.0, m_motionPercent + 1.0)) {
        m_motionPercent = percent;
        emit motionPercentChanged();
    }
    return percent >= m_wakePercent;
}

void FaceTracker::showWatchedFrame(const QImage &captured)
{
    if (m_previewViewers == 0 || captured.isNull()) {
        return;
    }
    setErrorString(QString{});
    setCaptureResolution(captured.size());
    const QImage image{captured.flipped(Qt::Horizontal)};
    QSize previewSize{image.size()};
    if (std::max(image.width(), image.height()) > m_previewMaxSide) {
        previewSize.scale(m_previewMaxSide, m_previewMaxSide, Qt::KeepAspectRatio);
    }
    setCameraFrame(previewSize == image.size()
                       ? image
                       : image.scaled(previewSize, Qt::KeepAspectRatio, Qt::FastTransformation));
}

void FaceTracker::attachPreview()
{
    ++m_previewViewers;
}

void FaceTracker::detachPreview()
{
    m_previewViewers = std::max(0, m_previewViewers - 1);
}

void FaceTracker::setThreshold(qreal &stored, qreal value, Kerfur::VisionThreshold which)
{
    const qreal bounded{std::clamp(value, 0.01, 0.99)};
    if (qFuzzyCompare(stored, bounded)) {
        return;
    }
    stored = bounded;
    if (m_worker != nullptr) {
        Kerfur::FaceAnalysisWorker *worker{m_worker};
        QMetaObject::invokeMethod(
            worker,
            [worker, which, bounded]() { worker->setThreshold(which, bounded); },
            Qt::QueuedConnection);
    }
    emit thresholdsChanged();
}

void FaceTracker::setPersonConfidence(qreal confidence)
{
    setThreshold(m_personConfidence, confidence, Kerfur::VisionThreshold::Person);
}

void FaceTracker::setKeypointConfidence(qreal confidence)
{
    setThreshold(m_keypointConfidence, confidence, Kerfur::VisionThreshold::Keypoint);
}

void FaceTracker::setIdentityThreshold(qreal similarity)
{
    setThreshold(m_identityThreshold, similarity, Kerfur::VisionThreshold::Identity);
}

void FaceTracker::setPalmConfidence(qreal confidence)
{
    setThreshold(m_palmConfidence, confidence, Kerfur::VisionThreshold::Palm);
}

void FaceTracker::setHandConfidence(qreal confidence)
{
    setThreshold(m_handConfidence, confidence, Kerfur::VisionThreshold::Hand);
}

void FaceTracker::setObjectConfidence(qreal confidence)
{
    setThreshold(m_objectConfidence, confidence, Kerfur::VisionThreshold::Object);
}

void FaceTracker::setCameraHeight(qreal metres)
{
    const qreal bounded{std::clamp(metres, 0.0, 3.0)};
    if (qFuzzyCompare(m_mount.heightMetres + 1.0, bounded + 1.0)) {
        return;
    }
    m_mount.heightMetres = bounded;
    emit mountChanged();
}

void FaceTracker::setCameraTilt(qreal degrees)
{
    const qreal bounded{std::clamp(degrees, -90.0, 90.0)};
    if (qFuzzyCompare(m_mount.tiltDegrees + 1000.0, bounded + 1000.0)) {
        return;
    }
    m_mount.tiltDegrees = bounded;
    emit mountChanged();
}

void FaceTracker::setScreenOffset(qreal metres)
{
    const qreal bounded{std::clamp(metres, -1.0, 1.0)};
    if (qFuzzyCompare(m_mount.screenOffsetMetres + 10.0, bounded + 10.0)) {
        return;
    }
    m_mount.screenOffsetMetres = bounded;
    emit mountChanged();
}

void FaceTracker::setFieldOfView(qreal degrees)
{
    // Zero is automatic; anything else is a lens a pinhole model can describe.
    const qreal bounded{degrees <= 0.0 ? 0.0 : std::clamp(degrees, 20.0, 170.0)};
    if (qFuzzyCompare(m_fieldOfView + 1.0, bounded + 1.0)) {
        return;
    }
    m_fieldOfView = bounded;
    emit mountChanged();
}

void FaceTracker::setIdleWatch(bool enabled)
{
    if (m_idleWatch == enabled) {
        return;
    }
    m_idleWatch = enabled;
    if (!enabled) {
        stopWatching(false);
    }
    m_idleClock.setEnabled(enabled);
    emit idleWatchChanged();
}

void FaceTracker::setIdleAfterSeconds(int seconds)
{
    const int bounded{std::clamp(seconds, 5, 3600)};
    if (m_idleAfterSeconds == bounded) {
        return;
    }
    m_idleAfterSeconds = bounded;
    applyIdleLimits();
    emit idleWatchChanged();
}

void FaceTracker::setWatchHz(qreal hertz)
{
    const qreal bounded{std::clamp(hertz, 0.2, 10.0)};
    if (qFuzzyCompare(m_watchHz, bounded)) {
        return;
    }
    m_watchHz = bounded;
    m_frameThrottleTimer.setInterval(frameInterval());
    emit idleWatchChanged();
}

void FaceTracker::setWakePercent(qreal percent)
{
    const qreal bounded{std::clamp(percent, 0.05, 50.0)};
    if (qFuzzyCompare(m_wakePercent, bounded)) {
        return;
    }
    m_wakePercent = bounded;
    emit idleWatchChanged();
}

void FaceTracker::setProbeSeconds(qreal seconds)
{
    const qreal bounded{std::clamp(seconds, 1.0, 60.0)};
    if (qFuzzyCompare(m_probeSeconds, bounded)) {
        return;
    }
    m_probeSeconds = bounded;
    applyIdleLimits();
    emit idleWatchChanged();
}

QRectF FaceTracker::faceBox() const
{
    return m_primaryFace.value(QStringLiteral("box")).toRectF();
}

QString FaceTracker::identity() const
{
    return m_primaryFace.value(QStringLiteral("identity")).toString();
}

qreal FaceTracker::identityConfidence() const
{
    return m_primaryFace.value(QStringLiteral("identityConfidence")).toDouble();
}

QString FaceTracker::emotion() const
{
    return m_primaryFace.value(QStringLiteral("emotion")).toString();
}

qreal FaceTracker::emotionConfidence() const
{
    return m_primaryFace.value(QStringLiteral("emotionConfidence")).toDouble();
}

int FaceTracker::attentionPriority() const
{
    const QVariant priority{m_primaryFace.value(QStringLiteral("priority"))};
    return priority.isValid() ? priority.toInt() : Kerfur::IdentityRegistry::unknownPriority;
}

QStringList FaceTracker::heldLabels() const
{
    QStringList labels;
    labels.reserve(m_held.size());
    for (const QVariant &entry : m_held) {
        const QString label{entry.toMap().value(QStringLiteral("label")).toString()};
        if (!label.isEmpty() && !labels.contains(label)) {
            labels.append(label);
        }
    }
    return labels;
}

void FaceTracker::setAttention(const QVariantList &hands, const QVariantList &held)
{
    if (m_hands == hands && m_held == held) {
        return;
    }
    m_hands = hands;
    m_held = held;
    // Shares `peopleChanged` with the skeleton rather than carrying its own
    // signal: both describe the same body, and a view that redrew one without
    // the other would draw a hand box on last frame's arm.
    emit peopleChanged();
}

QString FaceTracker::gazeDirection() const
{
    return m_primaryFace.value(QStringLiteral("gaze")).toString();
}

qreal FaceTracker::headYaw() const
{
    return m_primaryFace.value(QStringLiteral("yaw")).toDouble();
}

qreal FaceTracker::headPitch() const
{
    return m_primaryFace.value(QStringLiteral("pitch")).toDouble();
}

qreal FaceTracker::headRoll() const
{
    return m_primaryFace.value(QStringLiteral("roll")).toDouble();
}

QVariantList FaceTracker::skeletonEdges()
{
    return cocoSkeleton();
}

QVariantList FaceTracker::handEdges()
{
    return mediapipeHand();
}

QStringList FaceTracker::availableResolutions() const
{
    if (usingLibcamera()) {
        QStringList resolutions;
        for (const QSize &size : m_libcameras.at(m_preferredLibcamera).sizes) {
            resolutions.append(describeSize(size));
        }
        return resolutions;
    }
    if (m_preferredCamera.isNull()) {
        return {};
    }

    QSet<QString> unique;
    for (const QCameraFormat &format : m_preferredCamera.videoFormats()) {
        unique.insert(describeSize(format.resolution()));
    }

    QStringList resolutions{unique.values()};
    std::sort(resolutions.begin(), resolutions.end(), [](const QString &a, const QString &b) {
        return a.section('x', 0, 0).toInt() > b.section('x', 0, 0).toInt();
    });
    return resolutions;
}

QString FaceTracker::currentResolution() const
{
    if (!cameraActive()) {
        return QStringLiteral("Unknown");
    }
    // Only once a frame has actually been seen. Reporting the requested format
    // here is exactly the mistake that made this property lie.
    if (m_captureResolution.isEmpty()) {
        return QStringLiteral("Starting...");
    }
    return describeSize(m_captureResolution);
}

QString FaceTracker::requestedResolution() const
{
    return describeSize(m_requestedResolution);
}

QString FaceTracker::analysisResolution() const
{
    return describeSize(m_analysisSize);
}

QString FaceTracker::previewResolution() const
{
    return m_cameraFrame.isNull() ? QStringLiteral("Starting...")
                                  : describeSize(m_cameraFrame.size());
}

void FaceTracker::setPreviewMaxSide(int side)
{
    const int bounded{std::clamp(side, minimumPreviewMaxSide, maximumPreviewMaxSide)};
    if (m_previewMaxSide == bounded) {
        return;
    }
    m_previewMaxSide = bounded;
    emit previewMaxSideChanged();
}

void FaceTracker::setUpdateHz(int hertz)
{
    const int bounded{std::clamp(hertz, minimumUpdateHz, maximumUpdateHz)};
    if (m_updateHz == bounded) {
        return;
    }
    m_updateHz = bounded;
    m_frameThrottleTimer.setInterval(frameInterval());
    emit updateHzChanged();
}

void FaceTracker::setLoneStrangerHands(bool enabled)
{
    if (m_loneStrangerHands == enabled) {
        return;
    }
    m_loneStrangerHands = enabled;
    // Queued with the value, so the worker never reads this object's state
    // from its own thread.
    if (m_worker != nullptr) {
        QMetaObject::invokeMethod(m_worker,
                                  &Kerfur::FaceAnalysisWorker::setLoneStrangerHands,
                                  Qt::QueuedConnection,
                                  enabled);
    }
    emit loneStrangerHandsChanged();
}

void FaceTracker::setResolution(const QString &resolution)
{
    const QStringList parts{resolution.split('x')};
    if (parts.size() != 2) {
        return;
    }

    bool widthOk{false};
    bool heightOk{false};
    const QSize requested{parts.at(0).toInt(&widthOk), parts.at(1).toInt(&heightOk)};
    if (!widthOk || !heightOk || requested.isEmpty() || m_requestedResolution == requested) {
        return;
    }

    m_requestedResolution = requested;
    emit currentResolutionChanged();

    if (m_enabled && cameraActive()) {
        setCameraFrame(QImage{});
        setFaceDetected(false);
        initializeCamera();
    }
}

void FaceTracker::setDetectionConfidence(qreal confidence)
{
    setThreshold(m_detectionConfidence, confidence, Kerfur::VisionThreshold::Face);
}

void FaceTracker::setTrackingSmoothing(qreal smoothing)
{
    const qreal bounded{std::clamp(smoothing, 0.0, 1.0)};
    if (qFuzzyCompare(m_trackingSmoothing, bounded)) {
        return;
    }
    m_trackingSmoothing = bounded;
    emit trackingSmoothingChanged();
}

void FaceTracker::setAnalysisMaxSide(int side)
{
    const int bounded{std::clamp(side, minimumAnalysisMaxSide, maximumAnalysisMaxSide)};
    if (m_analysisMaxSide == bounded) {
        return;
    }
    m_analysisMaxSide = bounded;
    emit analysisMaxSideChanged();
}

void FaceTracker::setFaceDetected(bool detected)
{
    if (m_faceDetected == detected) {
        return;
    }
    m_faceDetected = detected;
    emit faceDetectedChanged();
}

void FaceTracker::setFaceCenter(const QPointF &center)
{
    if (m_faceCenter == center) {
        return;
    }
    m_faceCenter = center;
    emit faceCenterChanged();
}

void FaceTracker::setErrorString(const QString &error)
{
    if (m_errorString == error) {
        return;
    }
    m_errorString = error;
    emit errorStringChanged();
}

void FaceTracker::setCameraFrame(const QImage &frame)
{
    m_cameraFrame = frame;
    emit cameraFrameChanged();
}

void FaceTracker::setCaptureResolution(const QSize &size)
{
    if (m_captureResolution == size) {
        return;
    }
    m_captureResolution = size;
    emit currentResolutionChanged();
}
