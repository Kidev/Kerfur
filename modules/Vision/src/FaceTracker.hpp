#ifndef KERFUR_VISION_FACETRACKER_HPP
#define KERFUR_VISION_FACETRACKER_HPP

#include <opencv2/objdetect.hpp>
#include <opencv2/opencv.hpp>
#include <QCamera>
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

public:
    explicit FaceTracker(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool faceDetected() const { return m_faceDetected; }
    QPointF faceCenter() const { return m_faceCenter; }
    QString errorString() const { return m_errorString; }

signals:
    void enabledChanged();
    void faceDetectedChanged();
    void faceCenterChanged();
    void errorStringChanged();

private slots:
    void processVideoFrame(const QVideoFrame &frame);

private:
    void initializeCamera();
    void uninitializeCamera();
    void setFaceDetected(bool detected);
    void setFaceCenter(const QPointF &center);
    void setErrorString(const QString &error);

    bool m_enabled {false};
    bool m_faceDetected {false};
    QPointF m_faceCenter {};
    QString m_errorString {};

    QCamera m_camera;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    cv::CascadeClassifier m_faceCascade;

    static constexpr int PROCESSING_INTERVAL_MS {100}; // Process every 100ms
};

#endif /* KERFUR_VISION_FACETRACKER_HPP */
