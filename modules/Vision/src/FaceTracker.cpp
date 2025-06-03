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

    qDebug() << "FaceTracker: Initializing camera:" << m_preferredCamera.description();

    // Stop any existing camera
    uninitializeCamera();

    // Configure camera
    m_camera.setCameraDevice(m_preferredCamera);

    // Try to set a more reasonable resolution for better performance
    const auto formats = m_preferredCamera.videoFormats();
    QCameraFormat bestFormat;

    // Look for 1280x720 or 640x480 formats for better performance
    for (const auto &format : formats) {
        QSize res = format.resolution();
        if ((res.width() == 1280 && res.height() == 720) ||
            (res.width() == 960 && res.height() == 540) ||
            (res.width() == 640 && res.height() == 480)) {
            bestFormat = format;
            qDebug() << "FaceTracker: Found suitable format:" << res << format.pixelFormat();
            break;
        }
    }

    // If we found a smaller format, use it
    if (!bestFormat.resolution().isEmpty()) {
        m_camera.setCameraFormat(bestFormat);
        qDebug() << "FaceTracker: Set camera format to:" << bestFormat.resolution();
    } else {
        qDebug() << "FaceTracker: Using default camera format";
    }

    // Setup error handling
    connect(&m_camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error error, const QString &errorString) {
                qWarning() << "FaceTracker: Camera error" << error << ":" << errorString;
                setErrorString(QString("Camera error: %1").arg(errorString));

                // Retry camera initialization after a delay
                if (m_enabled && !m_cameraRetryTimer.isActive()) {
                    qDebug() << "FaceTracker: Scheduling camera retry...";
                    m_cameraRetryTimer.start();
                }
            });

    // Monitor camera state changes
    connect(&m_camera, &QCamera::activeChanged, this, [this](bool active) {
        qDebug() << "FaceTracker: Camera active changed to:" << active;
        if (active) {
            qDebug() << "FaceTracker: Camera started successfully!";
            setErrorString("");
            m_cameraRetryTimer.stop(); // Cancel any pending retries
        }
    });

    // Setup video frame processing with better error handling
    connect(&m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        // Only process if enabled and no critical errors
        if (m_enabled && m_errorString.isEmpty()) {
            processVideoFrame(frame);
        }
    });

    // Configure capture session
    m_captureSession.setCamera(&m_camera);
    m_captureSession.setVideoSink(&m_videoSink);

    // Start camera
    qDebug() << "FaceTracker: Starting camera...";
    m_camera.start();

    // Set timeout for camera initialization
    QTimer::singleShot(5000, this, [this]() {
        if (m_enabled && !m_camera.isActive()) {
            setErrorString("Camera failed to start within 5 seconds");
            qWarning() << "FaceTracker: Camera initialization timeout";
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
        qDebug() << "FaceTracker: cameraFrameBase64() called but frame is null";
        return QString();
    }

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);

    // Save as JPEG with reasonable quality for better performance
    bool success = m_cameraFrame.save(&buffer, "JPEG", 75);

    if (!success) {
        qWarning() << "FaceTracker: Failed to save image to buffer";
        return QString();
    }

    QString result = QString("data:image/jpeg;base64,") + ba.toBase64();

    static int debugCount = 0;
    if (debugCount < 3) {
        qDebug() << "FaceTracker: Generated base64 string length:" << result.length();
        debugCount++;
    }

    return result;
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled || m_faceCascade.empty())
        return;

    // Throttle processing to avoid overwhelming the system
    if (m_frameThrottleTimer.isActive())
        return;
    m_frameThrottleTimer.start();

    static int frameCount = 0;
    frameCount++;

    // Log first few frames and periodically
    if (frameCount <= 3 || frameCount % 120 == 0) {
        qDebug() << "FaceTracker: Processing frame" << frameCount << "Size:" << frame.size()
                 << "Format:" << frame.pixelFormat();
    }

    // Use Qt6's built-in conversion method
    QImage image = frame.toImage();

    if (image.isNull()) {
        if (frameCount <= 10) {
            qWarning() << "FaceTracker: Failed to convert frame to QImage using toImage(), format:"
                       << frame.surfaceFormat().pixelFormat();
        }
        return;
    }

    if (frameCount <= 3) {
        qDebug() << "FaceTracker: Successfully converted frame using toImage()"
                 << "Size:" << image.size() << "Format:" << image.format();
    }

    // Mirror image horizontally for more natural front-camera experience
    image = image.mirrored(true, false);

    // Scale images for different purposes
    // 1. Small image for display in UI (240p for better UI performance)
    const QImage displayImage =
        image.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 2. Medium image for face detection (480p for good detection vs performance balance)
    const QImage detectImage = image.scaled(640, 480, Qt::KeepAspectRatio, Qt::FastTransformation);

    // Set the display frame (this goes to the UI)
    setCameraFrame(displayImage);

    if (frameCount <= 3) {
        qDebug() << "FaceTracker: Display image size:" << displayImage.size()
                 << "Detect image size:" << detectImage.size();
    }

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
