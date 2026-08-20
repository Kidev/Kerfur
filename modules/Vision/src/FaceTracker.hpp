// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "faceanalysis.h"

#include <QCamera>
#include <QCameraDevice>
#include <QImage>
#include <QMediaCaptureSession>
#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVideoFrame>
#include <QVideoSink>

namespace Kerfur {
class FaceAnalysisWorker;
}

/// The camera, and everything Kerfur can say about what is in front of it.
///
/// It owns the capture device and a worker thread; the models themselves live
/// on the worker and are never touched from here. What crosses the boundary is
/// one image in and one AnalysisResult out, which is what keeps a pose pass off
/// the thread that has to draw the face sixty times a second.
///
/// All geometry this exposes is normalized to the analysed frame. The preview
/// image, the widget it is drawn into, the popup and the camera itself are four
/// different sizes, and every overlay bug this class has ever had came from a
/// coordinate that did not say which of them it was in.
class FaceTracker : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool faceDetected READ faceDetected NOTIFY faceDetectedChanged)
    Q_PROPERTY(QPointF faceCenter READ faceCenter NOTIFY faceCenterChanged)
    Q_PROPERTY(QRectF faceBox READ faceBox NOTIFY facesChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QImage cameraFrame READ cameraFrame NOTIFY cameraFrameChanged)
    Q_PROPERTY(QString cameraFrameBase64 READ cameraFrameBase64 NOTIFY cameraFrameChanged)

    /// Every face this pass, newest first by area. Each entry is a map of
    /// `box`, `landmarks`, `identity`, `identityConfidence`, `emotion`,
    /// `emotionConfidence`, `yaw`, `pitch`, `roll`, `gaze` and `known`.
    Q_PROPERTY(QVariantList faces READ faces NOTIFY facesChanged)

    /// Every person the pose stage found: `box` plus seventeen `keypoints` of
    /// `x`, `y` and `score`.
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)

    /// The bone list the keypoints are drawn with, as index pairs. Published
    /// from here rather than written out in QML because the keypoint order is
    /// the model's and not the view's.
    Q_PROPERTY(QVariantList skeletonEdges READ skeletonEdges CONSTANT)

    /// The five YuNet points of the primary face, normalized. Kept as its own
    /// property because the overlay draws them from a Repeater and a nested
    /// list in `faces` would rebuild every delegate on every frame.
    Q_PROPERTY(QVariantList faceLandmarks READ faceLandmarks NOTIFY facesChanged)

    Q_PROPERTY(QString identity READ identity NOTIFY facesChanged)
    Q_PROPERTY(qreal identityConfidence READ identityConfidence NOTIFY facesChanged)
    Q_PROPERTY(QString emotion READ emotion NOTIFY facesChanged)
    Q_PROPERTY(qreal emotionConfidence READ emotionConfidence NOTIFY facesChanged)
    Q_PROPERTY(QString gazeDirection READ gazeDirection NOTIFY facesChanged)
    Q_PROPERTY(qreal headYaw READ headYaw NOTIFY facesChanged)
    Q_PROPERTY(qreal headPitch READ headPitch NOTIFY facesChanged)
    Q_PROPERTY(qreal headRoll READ headRoll NOTIFY facesChanged)

    /// What the camera is actually delivering, which is not always what it was
    /// asked for. Read from the frames themselves rather than from the format
    /// that was requested, because a device that quietly substitutes a smaller
    /// mode reports the request back unchanged.
    Q_PROPERTY(QString currentResolution READ currentResolution NOTIFY currentResolutionChanged)
    Q_PROPERTY(QString requestedResolution READ requestedResolution NOTIFY
                   currentResolutionChanged)
    Q_PROPERTY(QString analysisResolution READ analysisResolution NOTIFY analysisChanged)
    Q_PROPERTY(int analysisMs READ analysisMs NOTIFY analysisChanged)
    Q_PROPERTY(QStringList availableResolutions READ availableResolutions NOTIFY
                   availableResolutionsChanged)

    Q_PROPERTY(QStringList knownIdentities READ knownIdentities NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString galleryDirectory READ galleryDirectory NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool identityReady READ identityReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool emotionReady READ emotionReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool poseReady READ poseReady NOTIFY capabilitiesChanged)

    Q_PROPERTY(qreal detectionConfidence READ detectionConfidence WRITE setDetectionConfidence
                   NOTIFY detectionConfidenceChanged)
    Q_PROPERTY(qreal trackingSmoothing READ trackingSmoothing WRITE setTrackingSmoothing NOTIFY
                   trackingSmoothingChanged)

public:
    explicit FaceTracker(QObject *parent = nullptr);
    ~FaceTracker() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool faceDetected() const { return m_faceDetected; }
    QPointF faceCenter() const { return m_faceCenter; }
    QRectF faceBox() const;
    QString errorString() const { return m_errorString; }
    QImage cameraFrame() const { return m_cameraFrame; }
    QString cameraFrameBase64() const;

    QVariantList faces() const { return m_faces; }
    QVariantList people() const { return m_people; }
    QVariantList faceLandmarks() const { return m_faceLandmarks; }
    static QVariantList skeletonEdges();

    QString identity() const;
    qreal identityConfidence() const;
    QString emotion() const;
    qreal emotionConfidence() const;
    QString gazeDirection() const;
    qreal headYaw() const;
    qreal headPitch() const;
    qreal headRoll() const;

    QString currentResolution() const;
    QString requestedResolution() const;
    QString analysisResolution() const;
    int analysisMs() const { return static_cast<int>(m_analysisMs); }
    QStringList availableResolutions() const;

    QStringList knownIdentities() const { return m_capabilities.enrolled; }
    QString galleryDirectory() const { return m_capabilities.galleryDirectory; }
    bool identityReady() const { return m_capabilities.identityReady; }
    bool emotionReady() const { return m_capabilities.emotionReady; }
    bool poseReady() const { return m_capabilities.poseReady; }

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
    void facesChanged();
    void peopleChanged();
    void analysisChanged();
    void capabilitiesChanged();
    void availableResolutionsChanged();
    void currentResolutionChanged();
    void detectionConfidenceChanged();
    void trackingSmoothingChanged();

    /// To the worker thread. Not called directly: a direct call would run the
    /// models on whichever thread emitted.
    void analysisRequested(const QImage &image, quint64 sequence, bool mirrored);
    void confidenceChanged(double confidence);

private slots:
    void processVideoFrame(const QVideoFrame &frame);
    void retryCameraInitialization();
    void applyResult(const Kerfur::AnalysisResult &result);
    void applyCapabilities(const Kerfur::AnalysisCapabilities &capabilities);

private:
    void startWorker();
    void initializeCamera();
    void uninitializeCamera();
    void enumerateCameras();
    void selectCameraFormat();

    void setFaceDetected(bool detected);
    void setFaceCenter(const QPointF &center);
    void setErrorString(const QString &error);
    void setCameraFrame(const QImage &frame);
    void setCaptureResolution(const QSize &size);

    bool m_enabled{false};
    bool m_faceDetected{false};
    QPointF m_faceCenter{0.5, 0.5};
    QString m_errorString;
    QImage m_cameraFrame;

    QVariantList m_faces;
    QVariantList m_people;
    QVariantList m_faceLandmarks;
    QVariantMap m_primaryFace;
    Kerfur::AnalysisCapabilities m_capabilities;
    QSize m_analysisSize;
    qint64 m_analysisMs{0};

    QCamera m_camera;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    QCameraDevice m_preferredCamera;
    QSize m_requestedResolution{1920, 1080};
    QSize m_captureResolution;

    QThread m_workerThread;
    Kerfur::FaceAnalysisWorker *m_worker{nullptr};
    bool m_analysisBusy{false};
    quint64 m_sequence{0};
    quint64 m_peopleRevision{0};

    qreal m_detectionConfidence{0.6};
    qreal m_trackingSmoothing{0.7};

    QTimer m_frameThrottleTimer;
    QTimer m_cameraRetryTimer;

    /// The preview is redrawn at ten frames a second. It is a diagnostic view
    /// in a control panel, not the face itself, and every frame past this costs
    /// a JPEG encode and a base64 of it.
    static constexpr int processingIntervalMs{100};

    /// The longer side the analysis image is reduced to. Large enough that an
    /// iris is still a dozen pixels across at conversational distance, small
    /// enough that the colour conversion is not itself the cost.
    static constexpr int analysisMaxSide{960};

    /// The longer side of the preview. Independent of the analysis size: this
    /// one is bounded by what the panel can show, that one by what the models
    /// can see.
    static constexpr int previewMaxSide{640};

    bool m_hasLastKnownPosition{false};
    bool m_isInitializing{false};
};
