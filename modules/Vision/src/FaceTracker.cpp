// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FaceTracker.hpp"

#include "faceanalysisworker.h"

#include <QBuffer>
#include <QCameraFormat>
#include <QDebug>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

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
    };
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
                       {QStringLiteral("keypoints"), keypoints}};
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
    , m_frameThrottleTimer{this}
    , m_cameraRetryTimer{this}
{
    Kerfur::ensureFaceAnalysisMetaTypesRegistered();

    m_frameThrottleTimer.setSingleShot(true);
    m_frameThrottleTimer.setInterval(processingIntervalMs);

    m_cameraRetryTimer.setSingleShot(true);
    m_cameraRetryTimer.setInterval(2000);
    connect(&m_cameraRetryTimer, &QTimer::timeout, this, &FaceTracker::retryCameraInitialization);

    startWorker();
    enumerateCameras();
}

FaceTracker::~FaceTracker()
{
    // Before the thread goes away, so that a pass in flight cannot land on a
    // half destroyed tracker.
    disconnect(&m_videoSink, nullptr, this, nullptr);
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
    connect(this,
            &FaceTracker::confidenceChanged,
            m_worker,
            &Kerfur::FaceAnalysisWorker::setFaceConfidence);
    connect(m_worker,
            &Kerfur::FaceAnalysisWorker::resultReady,
            this,
            &FaceTracker::applyResult);
    connect(m_worker, &Kerfur::FaceAnalysisWorker::ready, this, &FaceTracker::applyCapabilities);

    m_workerThread.setObjectName(QStringLiteral("kerfur-vision"));
    m_workerThread.start();

    // Loading four models and embedding the gallery takes seconds. Queued, so
    // that none of it happens before the application has finished starting.
    QMetaObject::invokeMethod(m_worker,
                              &Kerfur::FaceAnalysisWorker::initialize,
                              Qt::QueuedConnection);
}

void FaceTracker::enumerateCameras()
{
    const QList<QCameraDevice> cameras{QMediaDevices::videoInputs()};

    for (const QCameraDevice &camera : cameras) {
        if (camera.position() == QCameraDevice::FrontFace && m_preferredCamera.isNull()) {
            m_preferredCamera = camera;
        }
    }

    if (m_preferredCamera.isNull() && !cameras.isEmpty()) {
        m_preferredCamera = QMediaDevices::defaultVideoInput();
    }
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;

    if (m_enabled) {
        initializeCamera();
    } else {
        m_hasLastKnownPosition = false;
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
    // simply the largest there is. Ties are broken on frame rate, because two
    // modes of the same size differ only in how fast and in which pixel format
    // they arrive, and the faster one is always the better preview.
    const auto better = [](const QCameraFormat &candidate, const QCameraFormat &incumbent) {
        return candidate.maxFrameRate() > incumbent.maxFrameRate();
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
    m_isInitializing = true;

    if (m_preferredCamera.isNull()) {
        enumerateCameras();
    }
    if (m_preferredCamera.isNull()) {
        setErrorString(QStringLiteral("No cameras available"));
        m_isInitializing = false;
        return;
    }

    uninitializeCamera();

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
            // Only the model problem survives a successful start. Anything else
            // was about the camera, and the camera is now running.
            if (m_capabilities.problem.isEmpty()) {
                setErrorString(QString{});
            } else {
                setErrorString(m_capabilities.problem);
            }
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

    QTimer::singleShot(5000, this, [this]() {
        if (m_enabled && !m_camera.isActive()) {
            setErrorString(QStringLiteral("Camera failed to start within 5 seconds"));
            m_isInitializing = false;
        }
    });
}

void FaceTracker::retryCameraInitialization()
{
    if (m_enabled && !m_camera.isActive()) {
        qCInfo(trackerLog) << "retrying camera initialization";
        enumerateCameras();
        initializeCamera();
    }
}

void FaceTracker::uninitializeCamera()
{
    m_frameThrottleTimer.stop();
    m_cameraRetryTimer.stop();

    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged, this, nullptr);
    disconnect(&m_camera, &QCamera::errorOccurred, this, nullptr);
    disconnect(&m_camera, &QCamera::activeChanged, this, nullptr);

    if (m_camera.isActive()) {
        m_camera.stop();
    }

    setFaceDetected(false);
    setCameraFrame(QImage{});
    setCaptureResolution(QSize{});

    if (!m_faces.isEmpty() || !m_people.isEmpty()) {
        m_faces.clear();
        m_faceLandmarks.clear();
        m_primaryFace.clear();
        m_people.clear();
        emit facesChanged();
        emit peopleChanged();
    }
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled) {
        return;
    }

    if (m_frameThrottleTimer.isActive()) {
        return;
    }
    m_frameThrottleTimer.start();

    const QImage captured{frame.toImage()};
    if (captured.isNull()) {
        return;
    }

    // The truth about the resolution, taken from a frame that actually arrived
    // rather than from the format that was asked for.
    setCaptureResolution(captured.size());

    // Mirrored, and everything downstream works in the mirrored frame: the
    // preview, the overlay geometry and `faceCenter`, which is what makes the
    // rendered eyes follow the viewer the way a mirror would. The one place the
    // reflection has to be undone is the head pose solve, and the worker is
    // told that it is looking at one.
    const QImage image{captured.flipped(Qt::Horizontal)};

    QSize previewSize{image.size()};
    previewSize.scale(previewMaxSide, previewMaxSide, Qt::KeepAspectRatio);
    setCameraFrame(image.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // One pass at a time. Queueing a second while the first is still running
    // would build a backlog of images that are already out of date by the time
    // a model reaches them, which is the failure that turns a slow preview into
    // an unbounded one.
    if (m_analysisBusy || m_worker == nullptr) {
        return;
    }

    QSize analysisSize{image.size()};
    analysisSize.scale(analysisMaxSide, analysisMaxSide, Qt::KeepAspectRatio);
    const QImage analysisImage{analysisSize == image.size()
                                   ? image
                                   : image.scaled(analysisSize,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation)};

    m_analysisBusy = true;
    emit analysisRequested(analysisImage, ++m_sequence, true);
}

void FaceTracker::applyCapabilities(const Kerfur::AnalysisCapabilities &capabilities)
{
    m_capabilities = capabilities;
    emit capabilitiesChanged();

    if (!capabilities.problem.isEmpty()) {
        qCWarning(trackerLog) << capabilities.problem;
        setErrorString(capabilities.problem);
    } else {
        qCInfo(trackerLog) << "analysis ready; gallery" << capabilities.galleryDirectory
                           << "holds" << capabilities.enrolled;
        if (m_errorString.isEmpty() || m_camera.isActive()) {
            setErrorString(QString{});
        }
    }
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

    // Largest first: the nearest face is the one the panel labels and the one
    // the rendered eyes follow.
    QList<Kerfur::AnalysedFace> ordered{result.faces};
    std::sort(ordered.begin(),
              ordered.end(),
              [](const Kerfur::AnalysedFace &left, const Kerfur::AnalysedFace &right) {
                  return left.box.width() * left.box.height()
                         > right.box.width() * right.box.height();
              });

    QVariantList faces;
    faces.reserve(ordered.size());
    for (const Kerfur::AnalysedFace &face : ordered) {
        faces.append(faceToVariant(face));
    }
    m_faces = faces;

    if (ordered.isEmpty()) {
        m_primaryFace.clear();
        m_faceLandmarks.clear();
        emit facesChanged();

        setFaceDetected(false);
        if (!m_hasLastKnownPosition) {
            setFaceCenter(QPointF{0.5, 0.5});
        }
        return;
    }

    const Kerfur::AnalysedFace &primary{ordered.first()};
    m_primaryFace = faceToVariant(primary);
    m_faceLandmarks = landmarksToVariant(primary.landmarks);
    emit facesChanged();

    const QPointF centre{primary.box.center()};
    const qreal smoothing{std::clamp(m_trackingSmoothing, 0.0, 1.0)};
    if (m_hasLastKnownPosition && smoothing > 0.0) {
        setFaceCenter(QPointF{m_faceCenter.x() * smoothing + centre.x() * (1.0 - smoothing),
                              m_faceCenter.y() * smoothing + centre.y() * (1.0 - smoothing)});
    } else {
        setFaceCenter(centre);
    }
    m_hasLastKnownPosition = true;
    setFaceDetected(true);
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

QString FaceTracker::cameraFrameBase64() const
{
    if (m_cameraFrame.isNull()) {
        return QString{};
    }

    QByteArray encoded;
    QBuffer buffer{&encoded};
    buffer.open(QIODevice::WriteOnly);
    if (!m_cameraFrame.save(&buffer, "JPEG", 75)) {
        return QString{};
    }

    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(encoded.toBase64());
}

QStringList FaceTracker::availableResolutions() const
{
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
    if (!m_camera.isActive()) {
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

    if (m_enabled && m_camera.isActive()) {
        setCameraFrame(QImage{});
        setFaceDetected(false);
        initializeCamera();
    }
}

void FaceTracker::setDetectionConfidence(qreal confidence)
{
    const qreal bounded{std::clamp(confidence, 0.1, 0.95)};
    if (qFuzzyCompare(m_detectionConfidence, bounded)) {
        return;
    }
    m_detectionConfidence = bounded;
    emit detectionConfidenceChanged();
    emit confidenceChanged(m_detectionConfidence);
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
