#include "FaceTracker.hpp"
#include <QCameraDevice>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QVideoFrameFormat>

FaceTracker::FaceTracker(QObject *parent) : QObject(parent)
{
    // Load OpenCV face cascade classifier
    QString cascadePath {
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               "opencv4/haarcascades/haarcascade_frontalface_alt.xml")};

    if (cascadePath.isEmpty()) {
        // Try common vcpkg/system paths
        const QStringList
            fallbackPaths {"haarcascade_frontalface_alt.xml", // vcpkg usually puts this in PATH
                           "/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml",
                           "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml"};

        for (const QString &path : fallbackPaths) {
            if (QDir {}.exists(path) || m_faceCascade.load(path.toStdString())) {
                cascadePath = path;
                break;
            }
        }
    }

    if (!cascadePath.isEmpty() && m_faceCascade.load(cascadePath.toStdString())) {
        qDebug() << "Face cascade loaded successfully from:" << cascadePath;
    } else {
        setErrorString("Failed to load face detection cascade");
        qWarning() << "Failed to load face cascade";
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
        uninitializeCamera();
    }

    emit enabledChanged();
}

void FaceTracker::initializeCamera()
{
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
}

void FaceTracker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_enabled || m_faceCascade.empty())
        return;

    QVideoFrame clonedFrame {frame};
    if (!clonedFrame.map(QVideoFrame::ReadOnly))
        return;

    const QVideoFrameFormat format {clonedFrame.surfaceFormat()};

    // Convert QVideoFrame to QImage
    const QImage image {clonedFrame.bits(0),
                        format.frameWidth(),
                        format.frameHeight(),
                        clonedFrame.bytesPerLine(0),
                        QVideoFrameFormat::imageFormatFromPixelFormat(format.pixelFormat())};

    clonedFrame.unmap();

    if (image.isNull())
        return;

    // Convert to OpenCV Mat
    const QImage grayImage {image.convertToFormat(QImage::Format_Grayscale8)};
    cv::Mat cvImage {grayImage.height(),
                     grayImage.width(),
                     CV_8UC1,
                     (void *) grayImage.constBits(),
                     grayImage.bytesPerLine()};

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

        setFaceCenter(center);
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
