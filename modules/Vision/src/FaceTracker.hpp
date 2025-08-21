#pragma once

#include "AIFaceDetector.hpp"
#include <memory>
#include <QCamera>
#include <QCameraDevice>
#include <QImage>
#include <QMediaCaptureSession>
#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QSize>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

class FaceTracker : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool faceDetected READ faceDetected NOTIFY faceDetectedChanged)
    Q_PROPERTY(QPointF faceCenter READ faceCenter NOTIFY faceCenterChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QImage cameraFrame READ cameraFrame NOTIFY cameraFrameChanged)
    Q_PROPERTY(QPointF facePixelCenter READ facePixelCenter NOTIFY facePixelCenterChanged)
    Q_PROPERTY(QSize facePixelSize READ facePixelSize NOTIFY facePixelSizeChanged)
    Q_PROPERTY(QString cameraFrameBase64 READ cameraFrameBase64 NOTIFY cameraFrameChanged)
    Q_PROPERTY(QStringList availableResolutions READ availableResolutions NOTIFY
                   availableResolutionsChanged)
    Q_PROPERTY(QString currentResolution READ currentResolution NOTIFY currentResolutionChanged)
    Q_PROPERTY(qreal detectionConfidence READ detectionConfidence WRITE setDetectionConfidence
                   NOTIFY detectionConfidenceChanged)
    Q_PROPERTY(qreal trackingSmoothing READ trackingSmoothing WRITE setTrackingSmoothing NOTIFY
                   trackingSmoothingChanged)

public:
    explicit FaceTracker(QObject *parent = nullptr);
    ~FaceTracker();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool faceDetected() const { return m_faceDetected; }
    QPointF faceCenter() const { return m_faceCenter; }
    QString errorString() const { return m_errorString; }
    QImage cameraFrame() const { return m_cameraFrame; }
    QPointF facePixelCenter() const { return m_facePixelCenter; }
    QSize facePixelSize() const { return m_facePixelSize; }
    QString cameraFrameBase64() const;
    QStringList availableResolutions() const;
    QString currentResolution() const;
    qreal detectionConfidence() const { return m_detectionConfidence; }
    qreal trackingSmoothing() const { return m_trackingSmoothing; }

public slots:
    void setResolution(const QString &resolution);
    void setDetectionConfidence(qreal confidence);
    void setTrackingSmoothing(qreal smoothing);

signals:
    void enabledChanged();
    void faceDetectedChanged();
    void faceCenterChanged();
    void errorStringChanged();
    void cameraFrameChanged();
    void facePixelCenterChanged();
    void facePixelSizeChanged();
    void availableResolutionsChanged();
    void currentResolutionChanged();
    void detectionConfidenceChanged();
    void trackingSmoothingChanged();

private slots:
    void processVideoFrame(const QVideoFrame &frame);
    void retryCameraInitialization();

private:
    void initializeCamera();
    void uninitializeCamera();
    void enumerateCameras();
    void processFaceDetectionResults(const std::vector<AIFaceDetector::FaceInfo> &faces,
                                     const QImage &displayImage);

    void setFaceDetected(bool detected);
    void setFaceCenter(const QPointF &center);
    void setErrorString(const QString &error);
    void setCameraFrame(const QImage &frame);
    void setFacePixelCenter(const QPointF &center);
    void setFacePixelSize(const QSize &size);

    // Face tracking state
    bool m_enabled {false};
    bool m_faceDetected {false};
    QPointF m_faceCenter {0.5, 0.5};
    QString m_errorString;
    QImage m_cameraFrame;
    QPointF m_facePixelCenter;
    QSize m_facePixelSize;

    // Camera and multimedia
    QCamera m_camera;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    QCameraDevice m_preferredCamera;
    QSize m_requestedResolution {1920, 1080}; // Fixed at 1920x1080

    // AI Face detection
    std::unique_ptr<AIFaceDetector> m_faceDetector;

    // Detection parameters
    qreal m_detectionConfidence {0.7};
    qreal m_trackingSmoothing {0.3};

    // Timing and performance
    QTimer m_frameThrottleTimer;
    QTimer m_cameraRetryTimer;

    // Processing parameters
    static constexpr int PROCESSING_INTERVAL_MS {33}; // ~30 FPS
    QPointF m_lastKnownFaceCenter;
    QPointF m_lastKnownFacePixelCenter;
    QSize m_lastKnownFacePixelSize;
    bool m_hasLastKnownPosition {false};
    bool m_isInitializing {false};
};
