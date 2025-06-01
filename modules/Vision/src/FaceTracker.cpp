#include "FaceTracker.hpp"
#include <QCameraDevice>
#include <QDebug>
#include <QFile>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVideoFrameFormat>

FaceTracker::FaceTracker(QObject *parent)
    : QObject {parent}
    , m_camera {this}
    , m_captureSession {this}
    , m_videoSink {this}
    , m_frameThrottleTimer {this}
{
    // Load cascade from Qt resources
    QFile cascadeFile(":/Vision/opencv/haarcascade_frontalface_alt.xml");
    if (!cascadeFile.open(QIODevice::ReadOnly)) {
        setErrorString("Failed to open cascade file from resources");
        return;
    }

    // Create temporary file to load cascade (OpenCV needs file path)
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false); // Keep it for the lifetime of the app
    if (!tempFile.open()) {
        setErrorString("Failed to create temporary cascade file");
        return;
    }

    tempFile.write(cascadeFile.readAll());
    tempFile.close();

    if (m_faceCascade.load(tempFile.fileName().toStdString())) {
        setErrorString("");
    } else {
        setErrorString("Failed to load face detection cascade from resources");
    }

    // Setup frame throttling
    m_frameThrottleTimer.setSingleShot(true);
    m_frameThrottleTimer.setInterval(PROCESSING_INTERVAL_MS);
    connect(&m_frameThrottleTimer, &QTimer::timeout, this, [this]() {
        // Timer just ensures we don't process frames too frequently
    });
}

void FaceTracker::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

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

    // Find front-facing camera
    const QList<QCameraDevice> cameras {QMediaDevices::videoInputs()};
    QCameraDevice frontCamera {};

    for (const QCameraDevice &camera : cameras) {
        if (camera.position() == QCameraDevice::FrontFace) {
            frontCamera = camera;
            break;
        }
    }

    // Fallback to default camera if no front camera found
    if (frontCamera.isNull() && !cameras.isEmpty()) {
        frontCamera = QMediaDevices::defaultVideoInput();
    }

    if (frontCamera.isNull()) {
        setErrorString("No camera available");
        return;
    }

    m_camera.setCameraDevice(frontCamera);

    connect(&m_videoSink, &QVideoSink::videoFrameChanged, this, &FaceTracker::processVideoFrame);

    m_captureSession.setCamera(&m_camera);
    m_captureSession.setVideoSink(&m_videoSink);

    m_camera.start();
    setErrorString("");
}

void FaceTracker::uninitializeCamera()
{
    m_camera.stop();
    disconnect(&m_videoSink, &QVideoSink::videoFrameChanged, this, &FaceTracker::processVideoFrame);
    setFaceDetected(false);
    setCameraFrame(QImage());
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled || m_faceCascade.empty())
        return;

    // Throttle processing
    if (m_frameThrottleTimer.isActive())
        return;
    m_frameThrottleTimer.start();

    QVideoFrame clonedFrame {frame};
    if (!clonedFrame.map(QVideoFrame::ReadOnly))
        return;

    const QVideoFrameFormat format {clonedFrame.surfaceFormat()};

    // Convert QVideoFrame to QImage
    QImage image {clonedFrame.bits(0),
                  format.frameWidth(),
                  format.frameHeight(),
                  clonedFrame.bytesPerLine(0),
                  QVideoFrameFormat::imageFormatFromPixelFormat(format.pixelFormat())};

    clonedFrame.unmap();

    if (image.isNull())
        return;

    // Scale down for display (keep aspect ratio)
    const QImage displayImage {
        image.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)};
    setCameraFrame(displayImage);

    // Convert to OpenCV Mat for face detection
    const QImage grayImage {image.convertToFormat(QImage::Format_Grayscale8)};
    const qsizetype step {grayImage.bytesPerLine()};
    cv::Mat cvImage {grayImage.height(),
                     grayImage.width(),
                     CV_8UC1,
                     (void *) grayImage.constBits(),
                     static_cast<size_t>(step)};

    // Detect faces
    std::vector<cv::Rect> faces {};
    m_faceCascade.detectMultiScale(cvImage, faces, 1.3, 5, 0, cv::Size(50, 50));

    if (!faces.empty()) {
        // Use the largest face
        const cv::Rect &largestFace {
            *std::max_element(faces.begin(), faces.end(), [](const cv::Rect &a, const cv::Rect &b) {
                return a.area() < b.area();
            })};

        // Calculate face center as normalized coordinates (0.0 to 1.0)
        const QPointF center {static_cast<qreal>(largestFace.x + largestFace.width / 2) /
                                  cvImage.cols,
                              static_cast<qreal>(largestFace.y + largestFace.height / 2) /
                                  cvImage.rows};

        // Also calculate pixel coordinates for display
        const QPointF pixelCenter {static_cast<qreal>(largestFace.x + largestFace.width / 2) *
                                       displayImage.width() / cvImage.cols,
                                   static_cast<qreal>(largestFace.y + largestFace.height / 2) *
                                       displayImage.height() / cvImage.rows};

        const QSize pixelSize {static_cast<int>(largestFace.width * displayImage.width() /
                                                cvImage.cols),
                               static_cast<int>(largestFace.height * displayImage.height() /
                                                cvImage.rows)};

        setFaceCenter(center);
        setFacePixelCenter(pixelCenter);
        setFacePixelSize(pixelSize);
        setFaceDetected(true);
    } else {
        setFaceDetected(false);
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
