#pragma once

#include <opencv2/objdetect.hpp>
#include <opencv2/opencv.hpp>
#include <QCamera>
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

public:
    explicit FaceTracker(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool faceDetected() const { return m_faceDetected; }
    QPointF faceCenter() const { return m_faceCenter; }
    QString errorString() const { return m_errorString; }
    QImage cameraFrame() const { return m_cameraFrame; }
    QPointF facePixelCenter() const { return m_facePixelCenter; }
    QSize facePixelSize() const { return m_facePixelSize; }

signals:
    void enabledChanged();
    void faceDetectedChanged();
    void faceCenterChanged();
    void errorStringChanged();
    void cameraFrameChanged();
    void facePixelCenterChanged();
    void facePixelSizeChanged();

private slots:
    void processVideoFrame(const QVideoFrame &frame);

private:
    void initializeCamera();
    void uninitializeCamera();
    void setFaceDetected(bool detected);
    void setFaceCenter(const QPointF &center);
    void setErrorString(const QString &error);
    void setCameraFrame(const QImage &frame);
    void setFacePixelCenter(const QPointF &center);
    void setFacePixelSize(const QSize &size);

    bool m_enabled {false};
    bool m_faceDetected {false};
    QPointF m_faceCenter {};
    QString m_errorString {};
    QImage m_cameraFrame {};
    QPointF m_facePixelCenter {};
    QSize m_facePixelSize {};

    QCamera m_camera;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    cv::CascadeClassifier m_faceCascade;
    QTimer m_frameThrottleTimer;

    static constexpr int PROCESSING_INTERVAL_MS {100};
};
