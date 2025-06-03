#include "FaceTracker.hpp"
#include <QBuffer>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMediaDevices>
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
{
    qDebug() << "FaceTracker: Initializing...";

    // Setup frame throttling with shorter interval for smoother tracking
    m_frameThrottleTimer.setSingleShot(true);
    m_frameThrottleTimer.setInterval(PROCESSING_INTERVAL_MS);

    // Setup camera retry timer
    m_cameraRetryTimer.setSingleShot(true);
    m_cameraRetryTimer.setInterval(2000); // Retry after 2 seconds
    connect(&m_cameraRetryTimer, &QTimer::timeout, this, &FaceTracker::retryCameraInitialization);

    // Initialize camera devices list
    enumerateCameras();

    // Load OpenCV cascade
    initializeOpenCV();
}

void FaceTracker::enumerateCameras()
{
    const QList<QCameraDevice> cameras {QMediaDevices::videoInputs()};
    qDebug() << "FaceTracker: Found" << cameras.size() << "camera(s)";

    for (int i = 0; i < cameras.size(); ++i) {
        const QCameraDevice &camera = cameras[i];
        qDebug() << "  Camera" << i << ":";
        qDebug() << "    ID:" << camera.id();
        qDebug() << "    Description:" << camera.description();
        qDebug() << "    Position:" << camera.position();
        qDebug() << "    IsDefault:" << (camera == QMediaDevices::defaultVideoInput());

        // Prefer front-facing cameras for face tracking
        if (camera.position() == QCameraDevice::FrontFace && !m_preferredCamera.isNull()) {
            m_preferredCamera = camera;
            qDebug() << "    -> Set as preferred (front-facing)";
        }
    }

    // Fallback to default camera if no front-facing camera found
    if (m_preferredCamera.isNull() && !cameras.isEmpty()) {
        m_preferredCamera = QMediaDevices::defaultVideoInput();
        qDebug() << "FaceTracker: Using default camera:" << m_preferredCamera.description();
    }
}

void FaceTracker::initializeOpenCV()
{
    qDebug() << "FaceTracker: Loading OpenCV cascade...";

    // Load cascade from Qt resources
    QFile cascadeFile(":/Vision/opencv/haarcascade_frontalface_alt.xml");
    if (!cascadeFile.open(QIODevice::ReadOnly)) {
        setErrorString("Failed to open cascade file from resources");
        return;
    }

    // Create temporary file (OpenCV needs file path)
    m_tempCascadeFile = std::make_unique<QTemporaryFile>();
    m_tempCascadeFile->setAutoRemove(false); // Keep for app lifetime

    if (!m_tempCascadeFile->open()) {
        setErrorString("Failed to create temporary cascade file");
        return;
    }

    m_tempCascadeFile->write(cascadeFile.readAll());
    m_tempCascadeFile->close();

    if (m_faceCascade.load(m_tempCascadeFile->fileName().toStdString())) {
        qDebug() << "FaceTracker: OpenCV cascade loaded successfully";
        setErrorString("");
    } else {
        setErrorString("Failed to load face detection cascade");
    }
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    qDebug() << "FaceTracker: Setting enabled to" << enabled;
    m_enabled = enabled;

    if (m_enabled) {
        initializeCamera();
    } else {
        uninitializeCamera();
    }

    emit enabledChanged();
}

void FaceTracker::initializeCamera()
{
    if (m_faceCascade.empty()) {
        setErrorString("Face cascade not loaded");
        return;
    }

    if (m_preferredCamera.isNull()) {
        setErrorString("No cameras available");
        return;
    }

    // Stop any existing camera
    uninitializeCamera();

    // Configure camera
    m_camera.setCameraDevice(m_preferredCamera);

    // Try to set the requested resolution format
    if (m_requestedResolution.isValid()) {
        const auto formats = m_preferredCamera.videoFormats();
        QCameraFormat bestFormat;

        for (const auto &format : formats) {
            if (format.resolution() == m_requestedResolution) {
                bestFormat = format;
                break;
            }
        }

        if (!bestFormat.resolution().isEmpty()) {
            m_camera.setCameraFormat(bestFormat);
        }
    }

    // Setup error handling
    connect(&m_camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error error, const QString &errorString) {
                setErrorString(QString("Camera error: %1").arg(errorString));

                if (m_enabled && !m_cameraRetryTimer.isActive()) {
                    m_cameraRetryTimer.start();
                }
            });

    // Monitor camera state changes
    connect(&m_camera, &QCamera::activeChanged, this, [this](bool active) {
        if (active) {
            setErrorString("");
            m_cameraRetryTimer.stop();
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
        }
    });
}

void FaceTracker::retryCameraInitialization()
{
    if (m_enabled && !m_camera.isActive()) {
        qDebug() << "FaceTracker: Retrying camera initialization...";

        // Re-enumerate cameras in case devices changed
        enumerateCameras();

        // Try to reinitialize
        initializeCamera();
    }
}

void FaceTracker::uninitializeCamera()
{
    qDebug() << "FaceTracker: Uninitializing camera...";

    // Stop timers
    m_frameThrottleTimer.stop();
    m_cameraRetryTimer.stop();

    // Disconnect signals
    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged, this, nullptr);
    disconnect(&m_camera, &QCamera::errorOccurred, this, nullptr);
    disconnect(&m_camera, &QCamera::activeChanged, this, nullptr);

    // Stop camera
    if (m_camera.isActive()) {
        m_camera.stop();
    }

    // Reset state
    setFaceDetected(false);
    setCameraFrame(QImage());
}

QString FaceTracker::cameraFrameBase64() const
{
    if (m_cameraFrame.isNull()) {
        return QString();
    }

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);

    bool success = m_cameraFrame.save(&buffer, "JPEG", 75);
    if (!success) {
        return QString();
    }

    return QString("data:image/jpeg;base64,") + ba.toBase64();
}

QStringList FaceTracker::availableResolutions() const
{
    QStringList resolutions;
    if (m_preferredCamera.isNull()) {
        return resolutions;
    }

    const auto formats = m_preferredCamera.videoFormats();
    QSet<QString> uniqueResolutions;

    for (const auto &format : formats) {
        QSize res = format.resolution();
        QString resStr = QString("%1x%2").arg(res.width()).arg(res.height());
        uniqueResolutions.insert(resStr);
    }

    resolutions = uniqueResolutions.values();
    std::sort(resolutions.begin(), resolutions.end(), [](const QString &a, const QString &b) {
        QStringList aParts = a.split('x');
        QStringList bParts = b.split('x');
        int aWidth = aParts[0].toInt();
        int bWidth = bParts[0].toInt();
        return aWidth > bWidth; // Sort descending by width
    });

    return resolutions;
}

QString FaceTracker::currentResolution() const
{
    if (!m_camera.isActive()) {
        return "Unknown";
    }

    QCameraFormat format = m_camera.cameraFormat();
    QSize res = format.resolution();
    return QString("%1x%2").arg(res.width()).arg(res.height());
}

void FaceTracker::setResolution(const QString &resolution)
{
    QStringList parts = resolution.split('x');
    if (parts.size() != 2) {
        return;
    }

    bool ok1, ok2;
    int width = parts[0].toInt(&ok1);
    int height = parts[1].toInt(&ok2);

    if (!ok1 || !ok2) {
        return;
    }

    m_requestedResolution = QSize(width, height);

    // Restart camera with new resolution if currently active
    if (m_enabled) {
        initializeCamera();
    }

    emit currentResolutionChanged();
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled || m_faceCascade.empty())
        return;

    // Throttle processing
    if (m_frameThrottleTimer.isActive())
        return;
    m_frameThrottleTimer.start();

    // Use Qt6's built-in conversion method
    QImage image = frame.toImage();

    if (image.isNull()) {
        return;
    }

    // Mirror image horizontally for more natural experience
    image = image.mirrored(true, false);

    // Scale images for different purposes
    const QImage displayImage =
        image.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QImage detectImage = image.scaled(640, 480, Qt::KeepAspectRatio, Qt::FastTransformation);

    // Set the display frame
    setCameraFrame(displayImage);

    // Perform face detection
    detectFaces(detectImage, displayImage);
}

void FaceTracker::detectFaces(const QImage &detectImage, const QImage &displayImage)
{
    // Convert to grayscale for OpenCV processing
    const QImage grayImage = detectImage.convertToFormat(QImage::Format_Grayscale8);
    const qsizetype step = grayImage.bytesPerLine();

    cv::Mat cvImage {grayImage.height(),
                     grayImage.width(),
                     CV_8UC1,
                     (void *) grayImage.constBits(),
                     static_cast<size_t>(step)};

    // Detect faces with optimized parameters
    std::vector<cv::Rect> faces;
    m_faceCascade.detectMultiScale(cvImage,
                                   faces,
                                   1.1, // Scale factor (smaller = more thorough)
                                   4,   // Min neighbors (higher = fewer false positives)
                                   cv::CASCADE_SCALE_IMAGE, // Flags
                                   cv::Size(50, 50),        // Min face size
                                   cv::Size(300, 300)       // Max face size
    );

    if (!faces.empty()) {
        // Find largest face (closest to camera)
        const cv::Rect &largestFace = *std::max_element(faces.begin(),
                                                        faces.end(),
                                                        [](const cv::Rect &a, const cv::Rect &b) {
                                                            return a.area() < b.area();
                                                        });

        // Calculate normalized face center (0.0 to 1.0)
        const QPointF normalizedCenter {static_cast<qreal>(largestFace.x + largestFace.width / 2) /
                                            cvImage.cols,
                                        static_cast<qreal>(largestFace.y + largestFace.height / 2) /
                                            cvImage.rows};

        // Calculate pixel coordinates for display overlay
        const qreal scaleX = static_cast<qreal>(displayImage.width()) / detectImage.width();
        const qreal scaleY = static_cast<qreal>(displayImage.height()) / detectImage.height();

        const QPointF pixelCenter {(largestFace.x + largestFace.width / 2) * scaleX,
                                   (largestFace.y + largestFace.height / 2) * scaleY};

        const QSize pixelSize {static_cast<int>(largestFace.width * scaleX),
                               static_cast<int>(largestFace.height * scaleY)};

        // Update face tracking data
        setFaceCenter(normalizedCenter);
        setFacePixelCenter(pixelCenter);
        setFacePixelSize(pixelSize);
        setFaceDetected(true);

        // Clear any detection errors
        if (m_errorString.contains("No face")) {
            setErrorString("");
        }
    } else {
        setFaceDetected(false);
        // Don't set error for temporary face loss
    }
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
