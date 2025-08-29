#include "FaceTracker.hpp"
#include <QBuffer>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QVideoFrameFormat>

FaceTracker::FaceTracker(QObject *parent)
: QObject {parent}
, m_camera {this}
, m_captureSession {this}
, m_videoSink {this}
, m_frameThrottleTimer {this}
, m_cameraRetryTimer {this}
, m_requestedResolution {1920, 1080}
, m_detectionConfidence {0.6}      // Lower default confidence to detect more faces
, m_trackingSmoothing {0.7}        // Higher smoothing to reduce jitter
{
    // Initialize AI face detector
    m_faceDetector = std::make_unique<AIFaceDetector>();
    if (!m_faceDetector->isInitialized()) {
        setErrorString("Failed to initialize AI face detector: " + m_faceDetector->getLastError());
    }

    // Setup frame throttling - reduce frequency to reduce jitter
    m_frameThrottleTimer.setSingleShot(true);
    m_frameThrottleTimer.setInterval(PROCESSING_INTERVAL_MS);

    // Setup camera retry timer
    m_cameraRetryTimer.setSingleShot(true);
    m_cameraRetryTimer.setInterval(2000);
    connect(&m_cameraRetryTimer, &QTimer::timeout, this, &FaceTracker::retryCameraInitialization);

    // Initialize camera devices list
    enumerateCameras();
}

FaceTracker::~FaceTracker() = default;

void FaceTracker::enumerateCameras()
{
    const QList<QCameraDevice> cameras {QMediaDevices::videoInputs()};

    for (int i {0}; i < cameras.size(); ++i) {
        const QCameraDevice &camera {cameras[i]};

        // Prefer front-facing cameras for face tracking
        if (camera.position() == QCameraDevice::FrontFace && m_preferredCamera.isNull()) {
            m_preferredCamera = camera;
        }
    }

    // Fallback to default camera if no front-facing camera found
    if (m_preferredCamera.isNull() && !cameras.isEmpty()) {
        m_preferredCamera = QMediaDevices::defaultVideoInput();
    }
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (m_enabled) {
        initializeCamera();
    } else {
        // Reset last known position when disabling
        m_hasLastKnownPosition = false;
        m_lastKnownFaceCenter = QPointF {};
        m_lastKnownFacePixelCenter = QPointF {};
        m_lastKnownFacePixelSize = QSize {};
        uninitializeCamera();
    }

    emit enabledChanged();
}

void FaceTracker::initializeCamera()
{
    // Prevent concurrent initialization attempts
    if (m_isInitializing) {
        qDebug() << "Camera initialization already in progress";
        return;
    }
    m_isInitializing = true;

    if (!m_faceDetector || !m_faceDetector->isInitialized()) {
        setErrorString("Face detection models not loaded");
        m_isInitializing = false;
        return;
    }

    if (m_preferredCamera.isNull()) {
        setErrorString("No cameras available");
        m_isInitializing = false;
        return;
    }

    // Stop any existing camera completely
    uninitializeCamera();

    // Wait for camera to fully stop
    QThread::msleep(500);

    // Create a new camera instance to ensure clean state
    m_camera.setCameraDevice(m_preferredCamera);

    // Force 1920x1080 resolution
    const auto formats {m_preferredCamera.videoFormats()};
    QCameraFormat bestFormat {};

    // Find exact 1920x1080 format
    for (const auto &format : formats) {
        if (format.resolution() == QSize {1920, 1080}) {
            bestFormat = format;
            break;
        }
    }

    // If no exact match, find closest resolution (prefer larger)
    if (bestFormat.resolution().isEmpty()) {
        int targetPixels {1920 * 1080};
        int bestDiff {INT_MAX};

        for (const auto &format : formats) {
            QSize res {format.resolution()};
            // Prefer resolutions >= 1920x1080
            if (res.width() >= 1920 && res.height() >= 1080) {
                int pixels {res.width() * res.height()};
                int diff {pixels - targetPixels};
                if (diff < bestDiff) {
                    bestDiff = diff;
                    bestFormat = format;
                }
            }
        }

        // If still no match, use highest available resolution
        if (bestFormat.resolution().isEmpty() && !formats.isEmpty()) {
            bestFormat = formats.first();
            for (const auto &format : formats) {
                if (format.resolution().width() * format.resolution().height() >
                    bestFormat.resolution().width() * bestFormat.resolution().height()) {
                    bestFormat = format;
                }
            }
        }
    }

    if (!bestFormat.resolution().isEmpty()) {
        m_camera.setCameraFormat(bestFormat);
        qDebug() << "Setting camera format to:" << bestFormat.resolution();
    }

    // Setup error handling
    connect(&m_camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error error, const QString &errorString) {
                setErrorString(QString {"Camera error: %1"}.arg(errorString));

                if (m_enabled && !m_cameraRetryTimer.isActive()) {
                    m_cameraRetryTimer.start();
                }
            });

    // Monitor camera state changes
    connect(&m_camera, &QCamera::activeChanged, this, [this](bool active) {
        if (active) {
            setErrorString("");
            m_cameraRetryTimer.stop();
            m_isInitializing = false;

            QCameraFormat actualFormat {m_camera.cameraFormat()};
            qDebug() << "Camera started with resolution:" << actualFormat.resolution();

            emit currentResolutionChanged();
        } else if (m_isInitializing) {
            m_isInitializing = false;
            setErrorString("Camera failed to activate");
        }
    });

    // Setup video frame processing
    connect(&m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_enabled && m_errorString.isEmpty()) {
            processVideoFrame(frame);
        }
    });

    // Configure capture session
    m_captureSession.setCamera(&m_camera);
    m_captureSession.setVideoSink(&m_videoSink);

    // Start camera
    m_camera.start();

    // Set timeout for camera initialization
    QTimer::singleShot(5000, this, [this]() {
        if (m_enabled && !m_camera.isActive()) {
            setErrorString("Camera failed to start within 5 seconds");
            m_isInitializing = false;
        }
    });
}

void FaceTracker::retryCameraInitialization()
{
    if (m_enabled && !m_camera.isActive()) {
        qDebug() << "Retrying camera initialization...";
        enumerateCameras();
        initializeCamera();
    }
}

void FaceTracker::uninitializeCamera()
{
    // Stop timers
    m_frameThrottleTimer.stop();
    m_cameraRetryTimer.stop();

    // Disconnect signals to prevent callbacks during shutdown
    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged, this, nullptr);
    disconnect(&m_camera, &QCamera::errorOccurred, this, nullptr);
    disconnect(&m_camera, &QCamera::activeChanged, this, nullptr);

    // Stop camera and wait
    if (m_camera.isActive()) {
        m_camera.stop();
        // Give camera time to fully stop
        QThread::msleep(50);
    }

    // Reset state
    setFaceDetected(false);
    setCameraFrame(QImage {});
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled || !m_faceDetector || !m_faceDetector->isInitialized())
        return;

    // Throttle processing
    if (m_frameThrottleTimer.isActive())
        return;
    m_frameThrottleTimer.start();

    // Convert frame to QImage
    QImage image {frame.toImage()};
    if (image.isNull()) {
        return;
    }

    // Mirror image horizontally for more natural experience (using Qt 6.10 API)
    image = image.flipped(Qt::Horizontal);

    // Scale for display (maintain aspect ratio with max dimension of 640)
    int displayMaxDim {640};
    QSize displaySize {image.size()};
    if (displaySize.width() > displayMaxDim || displaySize.height() > displayMaxDim) {
        displaySize.scale(displayMaxDim, displayMaxDim, Qt::KeepAspectRatio);
    }

    const QImage displayImage {
        image.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation)};

    // Set the display frame
    setCameraFrame(displayImage);

    // Detect faces using AI (detector handles scaling internally for performance)
    auto faces {m_faceDetector->detectFaces(image, m_detectionConfidence)};

    // Process results
    processFaceDetectionResults(faces, displayImage);
}

void FaceTracker::processFaceDetectionResults(const std::vector<AIFaceDetector::FaceInfo> &faces,
                                              const QImage &displayImage)
{
    if (!faces.empty()) {
        // Use the face with highest confidence (already sorted by detector)
        const auto &bestFace {faces[0]};

        // Calculate normalized face center (0.0 to 1.0)
        QPointF faceCenter {bestFace.bbox.center()};
        const QPointF normalizedCenter {faceCenter.x() / displayImage.width(),
                                        faceCenter.y() / displayImage.height()};

        // Calculate pixel coordinates for display overlay
        const QPointF pixelCenter {faceCenter};
        const QSize pixelSize {static_cast<int>(bestFace.bbox.width()),
                               static_cast<int>(bestFace.bbox.height())};

        // Apply smoothing to reduce jitter
        if (m_hasLastKnownPosition && m_trackingSmoothing > 0.0) {
            QPointF smoothedCenter {};
            smoothedCenter.setX(m_lastKnownFaceCenter.x() * m_trackingSmoothing +
                                normalizedCenter.x() * (1.0 - m_trackingSmoothing));
            smoothedCenter.setY(m_lastKnownFaceCenter.y() * m_trackingSmoothing +
                                normalizedCenter.y() * (1.0 - m_trackingSmoothing));

            QPointF smoothedPixelCenter {};
            smoothedPixelCenter.setX(m_lastKnownFacePixelCenter.x() * m_trackingSmoothing +
                                     pixelCenter.x() * (1.0 - m_trackingSmoothing));
            smoothedPixelCenter.setY(m_lastKnownFacePixelCenter.y() * m_trackingSmoothing +
                                     pixelCenter.y() * (1.0 - m_trackingSmoothing));

            setFaceCenter(smoothedCenter);
            setFacePixelCenter(smoothedPixelCenter);
        } else {
            setFaceCenter(normalizedCenter);
            setFacePixelCenter(pixelCenter);
        }

        setFacePixelSize(pixelSize);
        setFaceDetected(true);

        // Convert landmarks to QVariantList for QML
        QVariantList landmarksList;
        for (const QPointF &landmark : bestFace.landmarks) {
            QVariantMap landmarkMap;
            landmarkMap["x"] = landmark.x();
            landmarkMap["y"] = landmark.y();
            landmarksList.append(landmarkMap);
        }
        setFaceLandmarks(landmarksList);

        // Store last known position (use member variables directly)
        m_lastKnownFaceCenter = m_faceCenter;
        m_lastKnownFacePixelCenter = m_facePixelCenter;
        m_lastKnownFacePixelSize = pixelSize;
        m_hasLastKnownPosition = true;

        if (m_errorString.contains("No face")) {
            setErrorString("");
        }
    } else {
        // No face detected - use last known position if available
        if (m_hasLastKnownPosition && m_enabled) {
            // Keep the last known positions but mark as not detected
            setFaceDetected(false);
            // Don't update face positions - keep the last known values
        } else {
            // No previous position available, reset everything
            setFaceDetected(false);
            setFaceCenter(QPointF {0.5, 0.5}); // Center position
            setFacePixelCenter(QPointF {displayImage.width() / 2.0, displayImage.height() / 2.0});
            setFacePixelSize(QSize {100, 100}); // Default size
            setFaceLandmarks(QVariantList{}); // Clear landmarks
        }
    }
}

QString FaceTracker::cameraFrameBase64() const
{
    if (m_cameraFrame.isNull()) {
        return QString {};
    }

    QByteArray ba {};
    QBuffer buffer {&ba};
    buffer.open(QIODevice::WriteOnly);

    bool success {m_cameraFrame.save(&buffer, "JPEG", 75)};
    if (!success) {
        return QString {};
    }

    return QString {"data:image/jpeg;base64,"} + ba.toBase64();
}

QStringList FaceTracker::availableResolutions() const
{
    QStringList resolutions {};
    if (m_preferredCamera.isNull()) {
        return resolutions;
    }

    const auto formats {m_preferredCamera.videoFormats()};
    QSet<QString> uniqueResolutions {};

    for (const auto &format : formats) {
        QSize res {format.resolution()};
        QString resStr {QString {"%1x%2"}.arg(res.width()).arg(res.height())};
        uniqueResolutions.insert(resStr);
    }

    resolutions = uniqueResolutions.values();
    std::sort(resolutions.begin(), resolutions.end(), [](const QString &a, const QString &b) {
        QStringList aParts {a.split('x')};
        QStringList bParts {b.split('x')};
        int aWidth {aParts[0].toInt()};
        int bWidth {bParts[0].toInt()};
        return aWidth > bWidth; // Sort descending by width
    });

    return resolutions;
}

QString FaceTracker::currentResolution() const
{
    if (!m_camera.isActive()) {
        return "Unknown";
    }

    QCameraFormat format {m_camera.cameraFormat()};
    QSize res {format.resolution()};
    return QString {"%1x%2"}.arg(res.width()).arg(res.height());
}

void FaceTracker::setResolution(const QString &resolution)
{
    QStringList parts {resolution.split('x')};
    if (parts.size() != 2) {
        return;
    }

    bool ok1 {false}, ok2 {false};
    int width {parts[0].toInt(&ok1)};
    int height {parts[1].toInt(&ok2)};

    if (!ok1 || !ok2) {
        return;
    }

    QSize newResolution {width, height};
    if (m_requestedResolution == newResolution) {
        return; // No change needed
    }

    m_requestedResolution = newResolution;

    // Force camera restart with new resolution if currently active
    if (m_enabled && m_camera.isActive()) {
        // Clear current frame to avoid showing stale data during restart
        setCameraFrame(QImage {});
        setFaceDetected(false);

        // Stop the camera completely first
        m_camera.stop();

        // Wait for camera to fully stop using event loop
        QEventLoop loop {};
        connect(&m_camera, &QCamera::activeChanged, &loop, [&loop](bool active) {
            if (!active) {
                loop.quit();
            }
        });

        // Set a timeout in case the signal doesn't fire
        QTimer::singleShot(1000, &loop, &QEventLoop::quit);

        // Wait for camera to stop
        if (m_camera.isActive()) {
            loop.exec();
        }

        // Additional safety delay
        QThread::msleep(200);

        // Now restart with new resolution
        initializeCamera();
    }

    emit currentResolutionChanged();
}

void FaceTracker::setDetectionConfidence(qreal confidence)
{
    if (qFuzzyCompare(m_detectionConfidence, confidence))
        return;

    m_detectionConfidence = qBound(0.1, confidence, 0.9);
    emit detectionConfidenceChanged();
}

void FaceTracker::setTrackingSmoothing(qreal smoothing)
{
    if (qFuzzyCompare(m_trackingSmoothing, smoothing))
        return;

    m_trackingSmoothing = qBound(0.0, smoothing, 1.0);
    emit trackingSmoothingChanged();
}

void FaceTracker::setFaceDetected(bool detected)
{
    if (m_faceDetected == detected)
        return;
    m_faceDetected = detected;
    emit faceDetectedChanged();
}

void FaceTracker::setFaceCenter(const QPointF &center)
{
    if (m_faceCenter == center)
        return;
    m_faceCenter = center;
    emit faceCenterChanged();
}

void FaceTracker::setErrorString(const QString &error)
{
    if (m_errorString == error)
        return;
    m_errorString = error;
    emit errorStringChanged();
}

void FaceTracker::setCameraFrame(const QImage &frame)
{
    if (m_cameraFrame == frame)
        return;
    m_cameraFrame = frame;
    emit cameraFrameChanged();
}

void FaceTracker::setFacePixelCenter(const QPointF &center)
{
    if (m_facePixelCenter == center)
        return;
    m_facePixelCenter = center;
    emit facePixelCenterChanged();
}

void FaceTracker::setFacePixelSize(const QSize &size)
{
    if (m_facePixelSize == size)
        return;
    m_facePixelSize = size;
    emit facePixelSizeChanged();
}

void FaceTracker::setFaceLandmarks(const QVariantList &landmarks)
{
    if (m_faceLandmarks == landmarks)
        return;
    m_faceLandmarks = landmarks;
    emit faceLandmarksChanged();
}
