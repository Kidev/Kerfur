// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "capturepermission.h"
#include "faceanalysis.h"
#include "libcameracapture.h"
#include "motiongate.h"
#include "scenegeometry.h"

#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QMediaCaptureSession>
#include <QMediaDevices>
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

#include <algorithm>

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
    /// Whether the eyes have somebody to follow: the face ranked first, or,
    /// when no face was found, the body ranked first. A body is followed from
    /// the first pass that sees it, before its face is found or named.
    Q_PROPERTY(bool following READ following NOTIFY followingChanged)
    Q_PROPERTY(QPointF faceCenter READ faceCenter NOTIFY faceCenterChanged)
    Q_PROPERTY(QRectF faceBox READ faceBox NOTIFY facesChanged)
    /// What stops the camera: permission, device, start-up. Empty while frames
    /// arrive, whatever the models are doing.
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    /// What the analysis is missing: models, gallery, enrolment. The preview
    /// runs without any of it, so this is a warning beside the picture and
    /// never a reason to withhold one.
    Q_PROPERTY(QString perceptionProblem READ perceptionProblem NOTIFY capabilitiesChanged)
    /// The preview picture, as an image rather than as an encoded string. It
    /// is handed straight to a CameraFrameView, which uploads it to a texture:
    /// there is no JPEG and no base64 anywhere in this path, because both used
    /// to be paid once per bound view per frame on the thread that draws the
    /// face.
    Q_PROPERTY(QImage cameraFrame READ cameraFrame NOTIFY cameraFrameChanged)

    /// Every face this pass, newest first by area. Each entry is a map of
    /// `box`, `landmarks`, `identity`, `identityConfidence`, `emotion`,
    /// `emotionConfidence`, `yaw`, `pitch`, `roll`, `gaze`, `known` and
    /// `title`.
    Q_PROPERTY(QVariantList faces READ faces NOTIFY facesChanged)

    /// Every person the pose stage found: `box`, seventeen `keypoints` of `x`,
    /// `y` and `score`, and -- for a person whose face the gallery recognised
    /// `identity`, `priority`, `title`, `hands` and `held`.
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)

    /// The bone list the keypoints are drawn with, as index pairs. Published
    /// from here rather than written out in QML because the keypoint order is
    /// the model's and not the view's.
    Q_PROPERTY(QVariantList skeletonEdges READ skeletonEdges CONSTANT)

    /// The twenty-one point hand skeleton, as index pairs into a hand's
    /// `landmarks`. Published here for the same reason the body bones are: the
    /// point order is MediaPipe's, and a view that hard coded it would be
    /// wrong silently the day the stage changed.
    Q_PROPERTY(QVariantList handEdges READ handEdges CONSTANT)

    /// The five YuNet points of the primary face, normalized. Kept as its own
    /// property because the overlay draws them from a Repeater and a nested
    /// list in `faces` would rebuild every delegate on every frame.
    Q_PROPERTY(QVariantList faceLandmarks READ faceLandmarks NOTIFY facesChanged)

    Q_PROPERTY(QString identity READ identity NOTIFY facesChanged)
    Q_PROPERTY(qreal identityConfidence READ identityConfidence NOTIFY facesChanged)
    Q_PROPERTY(QString emotion READ emotion NOTIFY facesChanged)
    Q_PROPERTY(qreal emotionConfidence READ emotionConfidence NOTIFY facesChanged)
    Q_PROPERTY(QString gazeDirection READ gazeDirection NOTIFY facesChanged)

    /// The rank of the face the eyes are following, zero being the top and
    /// `IdentityRegistry::unknownPriority` meaning a stranger. Exposed because
    /// the panel has to be able to show that Kerfur is looking at the right
    /// person for a reason and not by accident.
    Q_PROPERTY(int attentionPriority READ attentionPriority NOTIFY facesChanged)

    /// The hands of the person the eyes are following, as maps of `box`,
    /// `wrist`, `side` and `confidence`. Hands are located for people the
    /// gallery knows, and for a stranger only while `loneStrangerHands` is set
    /// and nobody else is in view.
    Q_PROPERTY(QVariantList hands READ hands NOTIFY peopleChanged)

    /// What that person is holding, as maps of `label`, `box`, `score` and
    /// `side`.
    Q_PROPERTY(QVariantList heldObjects READ heldObjects NOTIFY peopleChanged)

    /// The labels alone, for a one line readout.
    Q_PROPERTY(QStringList heldLabels READ heldLabels NOTIFY peopleChanged)
    Q_PROPERTY(qreal headYaw READ headYaw NOTIFY facesChanged)
    Q_PROPERTY(qreal headPitch READ headPitch NOTIFY facesChanged)
    Q_PROPERTY(qreal headRoll READ headRoll NOTIFY facesChanged)

    /// What the camera is actually delivering, which is not always what it was
    /// asked for. Read from the frames themselves rather than from the format
    /// that was requested, because a device that quietly substitutes a smaller
    /// mode reports the request back unchanged.
    Q_PROPERTY(QString currentResolution READ currentResolution NOTIFY currentResolutionChanged)
    /// The capture size asked for, as `WxH`. A camera that does not offer it
    /// delivers the smallest size it has that covers it, or its largest.
    Q_PROPERTY(QString requestedResolution READ requestedResolution WRITE setResolution NOTIFY
                   currentResolutionChanged)
    /// The longer side of the frames that arrive, or 0 before the first one.
    /// Neither the preview nor the analysis is ever enlarged past it.
    Q_PROPERTY(int captureMaxSide READ captureMaxSide NOTIFY currentResolutionChanged)
    Q_PROPERTY(QString analysisResolution READ analysisResolution NOTIFY analysisChanged)
    Q_PROPERTY(int analysisMs READ analysisMs NOTIFY analysisChanged)
    Q_PROPERTY(QStringList availableResolutions READ availableResolutions NOTIFY
                   availableResolutionsChanged)

    /// The video inputs, one distinct label each, and the label of the one in
    /// use. `cameraId` is the choice that persists: a device id, or empty for
    /// the automatic rule (front facing, then the system default). A chosen
    /// camera that is unplugged falls back to that rule and is taken back as
    /// soon as it returns.
    ///
    /// Cameras only libcamera can drive (a Raspberry Pi CSI module) are listed
    /// after the QCamera ones, with ids from `Kerfur::libcameraCameraId`.
    Q_PROPERTY(QStringList availableCameras READ availableCameras NOTIFY availableCamerasChanged)
    Q_PROPERTY(QString currentCamera READ currentCamera NOTIFY availableCamerasChanged)
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged)

    /// The longer side the analysis image is reduced to before the models see
    /// it, which is the one knob that trades detail for latency. Raising it
    /// lets a face across the room and a hand at arm's length survive the
    /// downscale; the cost is superlinear, so `analysisMs` is published beside
    /// it and the panel shows both together.
    Q_PROPERTY(int analysisMaxSide READ analysisMaxSide WRITE setAnalysisMaxSide NOTIFY
                   analysisMaxSideChanged)

    /// The longer side of the picture the panel is shown, which is the size
    /// `cameraFrame` is scaled to.
    ///
    /// Separate from the analysis size on purpose: one is bounded by what the
    /// models can use and the other by what a person can see. They are also
    /// paid for differently -- the analysis is one inference chain per frame,
    /// the preview is a resample and a texture upload -- so a machine that
    /// wants a sharp picture and a cheap analysis, or the reverse, can have
    /// either.
    Q_PROPERTY(int previewMaxSide READ previewMaxSide WRITE setPreviewMaxSide NOTIFY
                   previewMaxSideChanged)

    /// How many times a second a frame is taken from the camera, analysed and
    /// published. It is one number for both because they are one act: the
    /// picture is held back until the pass measured against it lands, so that
    /// an overlay is never drawn over a frame it was not measured on.
    ///
    /// This is the coarsest knob in the module. Every model in the chain runs
    /// at this rate, and the camera format is chosen to cover it rather than to
    /// be as fast as the device can manage.
    Q_PROPERTY(int updateHz READ updateHz WRITE setUpdateHz NOTIFY updateHzChanged)

    /// Locates, measures and searches the hands of somebody the gallery does
    /// not know, provided they are the only person in view. On by default; a
    /// room with two or more people keeps hands for the people Kerfur knows.
    Q_PROPERTY(bool loneStrangerHands READ loneStrangerHands WRITE setLoneStrangerHands NOTIFY
                   loneStrangerHandsChanged)

    /// Reads faces for an expression. Off by default; `emotionReady` says
    /// whether a model to read them with loaded.
    Q_PROPERTY(bool expressionEnabled READ expressionEnabled WRITE setExpressionEnabled NOTIFY
                   expressionEnabledChanged)

    /// The size the preview is actually being drawn at, which is the capture
    /// size reduced to `previewMaxSide` and never enlarged past it. Published
    /// because a panel that names one resolution while showing a picture built
    /// from a quarter as many pixels is the reason this property exists.
    Q_PROPERTY(QString previewResolution READ previewResolution NOTIFY cameraFrameChanged)

    Q_PROPERTY(QStringList knownIdentities READ knownIdentities NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString galleryDirectory READ galleryDirectory NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool identityReady READ identityReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool emotionReady READ emotionReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool poseReady READ poseReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool objectsReady READ objectsReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool handsReady READ handsReady NOTIFY capabilitiesChanged)

    /// The declared people in rank order, as `name (rank)`.
    Q_PROPERTY(QStringList roster READ roster NOTIFY capabilitiesChanged)

    /// The thresholds of the stages, each a score from 0 to 1: the face
    /// detector's (`detectionConfidence`), the pose model's for a person and
    /// for one keypoint, the cosine similarity a face needs to a gallery sample
    /// to be named, the palm detector's, the hand landmark model's, and the
    /// object detector's. Each model scores on its own scale, so each has its
    /// own.
    Q_PROPERTY(qreal detectionConfidence READ detectionConfidence WRITE setDetectionConfidence
                   NOTIFY thresholdsChanged)
    Q_PROPERTY(qreal personConfidence READ personConfidence WRITE setPersonConfidence NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal keypointConfidence READ keypointConfidence WRITE setKeypointConfidence NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal identityThreshold READ identityThreshold WRITE setIdentityThreshold NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal palmConfidence READ palmConfidence WRITE setPalmConfidence NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal handConfidence READ handConfidence WRITE setHandConfidence NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal objectConfidence READ objectConfidence WRITE setObjectConfidence NOTIFY
                   thresholdsChanged)
    Q_PROPERTY(qreal trackingSmoothing READ trackingSmoothing WRITE setTrackingSmoothing NOTIFY
                   trackingSmoothingChanged)

    /// Where the camera is: lens height above the floor in metres, tilt in
    /// degrees (0 straight ahead, 90 straight up, negative down), and how far
    /// below the camera the centre of the screen is, along the screen, in
    /// metres. The screen is taken to tilt with the camera.
    Q_PROPERTY(qreal cameraHeight READ cameraHeight WRITE setCameraHeight NOTIFY mountChanged)
    Q_PROPERTY(qreal cameraTilt READ cameraTilt WRITE setCameraTilt NOTIFY mountChanged)
    Q_PROPERTY(qreal screenOffset READ screenOffset WRITE setScreenOffset NOTIFY mountChanged)
    /// The horizontal field of view in degrees, or 0 for automatic: the known
    /// figure of a Raspberry Pi camera module, and `fallbackFieldOfView`
    /// otherwise. `effectiveFieldOfView` is the one in use.
    Q_PROPERTY(qreal fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY mountChanged)
    Q_PROPERTY(qreal effectiveFieldOfView READ effectiveFieldOfView NOTIFY mountChanged)

    /// Where the drawn eyes should look, from the centre of the screen
    /// towards the face being followed, in degrees: yaw positive towards the
    /// viewer's right, pitch positive downwards. `trackingSmoothing` filters
    /// small steps and lets large ones through (see `smoothingWeight`).
    Q_PROPERTY(qreal gazeYaw READ gazeYaw NOTIFY gazeChanged)
    Q_PROPERTY(qreal gazePitch READ gazePitch NOTIFY gazeChanged)
    /// How far along the floor the face being followed is, in metres, or -1
    /// when it could not be placed. Every entry of `faces` carries its own
    /// `distance` and the `distanceSource` it came from.
    Q_PROPERTY(qreal attentionDistance READ attentionDistance NOTIFY facesChanged)

    /// Idle watching. Once nobody has been seen for `idleAfterSeconds`, the
    /// models stop and frames are only compared with each other, at
    /// `watchHz`. When more than `wakePercent` of the picture changes the
    /// models run again; if they find nobody within `probeSeconds`, watching
    /// resumes.
    Q_PROPERTY(bool idleWatch READ idleWatch WRITE setIdleWatch NOTIFY idleWatchChanged)
    Q_PROPERTY(int idleAfterSeconds READ idleAfterSeconds WRITE setIdleAfterSeconds NOTIFY
                   idleWatchChanged)
    Q_PROPERTY(qreal watchHz READ watchHz WRITE setWatchHz NOTIFY idleWatchChanged)
    Q_PROPERTY(qreal wakePercent READ wakePercent WRITE setWakePercent NOTIFY idleWatchChanged)
    Q_PROPERTY(qreal probeSeconds READ probeSeconds WRITE setProbeSeconds NOTIFY idleWatchChanged)
    /// Whether the models are stopped and only motion is being looked for.
    Q_PROPERTY(bool watching READ watching NOTIFY watchingChanged)
    /// The share of the picture that changed at the last watched frame, as a
    /// percentage.
    Q_PROPERTY(qreal motionPercent READ motionPercent NOTIFY motionPercentChanged)

public:
    explicit FaceTracker(QObject *parent = nullptr);
    ~FaceTracker() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool faceDetected() const { return m_faceDetected; }
    bool following() const { return m_following; }
    QPointF faceCenter() const { return m_faceCenter; }
    QRectF faceBox() const;
    QString errorString() const { return m_errorString; }
    QString perceptionProblem() const { return m_capabilities.problem; }
    QImage cameraFrame() const { return m_cameraFrame; }

    QVariantList faces() const { return m_faces; }
    QVariantList people() const { return m_people; }
    QVariantList faceLandmarks() const { return m_faceLandmarks; }
    static QVariantList skeletonEdges();
    static QVariantList handEdges();

    QString identity() const;
    qreal identityConfidence() const;
    QString emotion() const;
    qreal emotionConfidence() const;
    QString gazeDirection() const;
    int attentionPriority() const;
    QVariantList hands() const { return m_hands; }
    QVariantList heldObjects() const { return m_held; }
    QStringList heldLabels() const;
    qreal headYaw() const;
    qreal headPitch() const;
    qreal headRoll() const;

    QString currentResolution() const;
    int captureMaxSide() const
    {
        return std::max(m_captureResolution.width(), m_captureResolution.height());
    }
    QString requestedResolution() const;
    QString analysisResolution() const;
    int analysisMs() const { return static_cast<int>(m_analysisMs); }
    QStringList availableResolutions() const;
    QStringList availableCameras() const { return m_cameraLabels; }
    QString currentCamera() const;
    QString cameraId() const { return QString::fromUtf8(m_wantedCamera); }
    /// The id behind one of `availableCameras`, or empty for a label that is
    /// not listed.
    Q_INVOKABLE QString cameraIdFor(const QString &label) const;
    int analysisMaxSide() const { return m_analysisMaxSide; }
    int previewMaxSide() const { return m_previewMaxSide; }
    int updateHz() const { return m_updateHz; }
    bool loneStrangerHands() const { return m_loneStrangerHands; }
    bool expressionEnabled() const { return m_expressionEnabled; }
    QString previewResolution() const;

    QStringList knownIdentities() const { return m_capabilities.enrolled; }
    QString galleryDirectory() const { return m_capabilities.galleryDirectory; }
    bool identityReady() const { return m_capabilities.identityReady; }
    bool emotionReady() const { return m_capabilities.emotionReady; }
    bool poseReady() const { return m_capabilities.poseReady; }
    bool objectsReady() const { return m_capabilities.objectsReady; }
    bool handsReady() const { return m_capabilities.handsReady; }
    QStringList roster() const { return m_capabilities.roster; }

    qreal detectionConfidence() const { return m_detectionConfidence; }
    qreal trackingSmoothing() const { return m_trackingSmoothing; }
    qreal personConfidence() const { return m_personConfidence; }
    qreal keypointConfidence() const { return m_keypointConfidence; }
    qreal identityThreshold() const { return m_identityThreshold; }
    qreal palmConfidence() const { return m_palmConfidence; }
    qreal handConfidence() const { return m_handConfidence; }
    qreal objectConfidence() const { return m_objectConfidence; }

    qreal cameraHeight() const { return m_mount.heightMetres; }
    qreal cameraTilt() const { return m_mount.tiltDegrees; }
    qreal screenOffset() const { return m_mount.screenOffsetMetres; }
    qreal fieldOfView() const { return m_fieldOfView; }
    qreal effectiveFieldOfView() const;
    qreal gazeYaw() const { return m_gaze.yawDegrees; }
    qreal gazePitch() const { return m_gaze.pitchDegrees; }
    qreal attentionDistance() const { return m_attentionDistance; }

    bool idleWatch() const { return m_idleWatch; }
    int idleAfterSeconds() const { return m_idleAfterSeconds; }
    qreal watchHz() const { return m_watchHz; }
    qreal wakePercent() const { return m_wakePercent; }
    qreal probeSeconds() const { return m_probeSeconds; }
    bool watching() const { return m_idleClock.isWatching(); }
    qreal motionPercent() const { return m_motionPercent; }

    /// A view showing `cameraFrame` says so, and says so again when it goes.
    /// While watching, the picture is converted to colour only when one is.
    Q_INVOKABLE void attachPreview();
    Q_INVOKABLE void detachPreview();

    /// The field of view assumed for a camera whose own is not known.
    static constexpr double fallbackFieldOfView{70.0};

    /// The step past which `trackingSmoothing` mostly lets a new position
    /// through: in degrees for the gaze, in frame widths for `faceCenter`.
    static constexpr double gazeSnapDegrees{4.0};
    static constexpr double centreSnapFraction{0.04};

public slots:
    void setResolution(const QString &resolution);
    void setCameraId(const QString &id);
    void setDetectionConfidence(qreal confidence);
    void setTrackingSmoothing(qreal smoothing);
    void setAnalysisMaxSide(int side);
    void setPreviewMaxSide(int side);
    void setUpdateHz(int hertz);
    void setLoneStrangerHands(bool enabled);
    void setExpressionEnabled(bool enabled);
    void setPersonConfidence(qreal confidence);
    void setKeypointConfidence(qreal confidence);
    void setIdentityThreshold(qreal similarity);
    void setPalmConfidence(qreal confidence);
    void setHandConfidence(qreal confidence);
    void setObjectConfidence(qreal confidence);
    void setCameraHeight(qreal metres);
    void setCameraTilt(qreal degrees);
    void setScreenOffset(qreal metres);
    void setFieldOfView(qreal degrees);
    void setIdleWatch(bool enabled);
    void setIdleAfterSeconds(int seconds);
    void setWatchHz(qreal hertz);
    void setWakePercent(qreal percent);
    void setProbeSeconds(qreal seconds);

signals:
    void enabledChanged();
    void faceDetectedChanged();
    void followingChanged();
    void faceCenterChanged();
    void errorStringChanged();
    void cameraFrameChanged();
    void facesChanged();
    void peopleChanged();
    void analysisChanged();
    void capabilitiesChanged();
    void availableResolutionsChanged();
    void availableCamerasChanged();
    void cameraIdChanged();
    void currentResolutionChanged();
    void thresholdsChanged();
    void trackingSmoothingChanged();
    void analysisMaxSideChanged();
    void previewMaxSideChanged();
    void updateHzChanged();
    void loneStrangerHandsChanged();
    void expressionEnabledChanged();
    void mountChanged();
    void gazeChanged();
    void idleWatchChanged();
    void watchingChanged();
    void motionPercentChanged();

    /// To the worker thread. Not called directly: a direct call would run the
    /// models on whichever thread emitted.
    void analysisRequested(const QImage &image, quint64 sequence, bool mirrored);

private slots:
    void processVideoFrame(const QVideoFrame &frame);
    void processLibcameraFrame();
    void retryCameraInitialization();
    void checkCameraStarted();
    void applyResult(const Kerfur::AnalysisResult &result);
    void applyCapabilities(const Kerfur::AnalysisCapabilities &capabilities);

private:
    void startWorker();
    void initializeCamera();
    void uninitializeCamera();
    /// Reopens on `m_preferredCamera`, also while a previous open is still
    /// waiting for the camera to report active.
    void restartCamera();
    /// Refreshes the device list and applies the choice rule. Returns whether
    /// the camera to use changed.
    bool enumerateCameras();
    void handleVideoInputsChanged();
    void selectCameraFormat();
    void startLibcamera();

    /// Whether the chosen camera is one only libcamera can drive.
    bool usingLibcamera() const { return m_preferredLibcamera >= 0; }
    bool hasPreferredCamera() const { return usingLibcamera() || !m_preferredCamera.isNull(); }
    /// The id of the chosen camera, in the same space as `cameraId`.
    QByteArray preferredCameraId() const;
    /// The id behind one entry of `m_cameraLabels`.
    QByteArray cameraIdAt(qsizetype index) const;
    /// Whether the open camera is running, whichever kind it is.
    bool cameraActive() const;

    /// The throttle and the one-pass-at-a-time latch, shared by both capture
    /// paths. True when this frame starts a pass, which restarts the throttle.
    bool acceptFrame();
    /// Everything after a captured frame has become an image.
    void processCapturedImage(const QImage &captured);

    /// Whether a pass can measure anything: a face detector or a pose model
    /// loaded. Without either, frames go straight to the preview.
    bool canAnalyse() const;

    void setFaceDetected(bool detected);
    void setFollowing(bool following);
    /// Points the eyes at `point`, normalized to the analysed frame, placed at
    /// `placement` when that is valid.
    void follow(const QPointF &point,
                const Kerfur::Placement &placement,
                const Kerfur::SceneGeometry &geometry);
    void setFaceCenter(const QPointF &center);
    void setErrorString(const QString &error);
    void setCameraFrame(const QImage &frame);
    void setCaptureResolution(const QSize &size);
    void setAttention(const QVariantList &hands, const QVariantList &held);

    /// The geometry of the current mount over an analysed frame.
    Kerfur::SceneGeometry sceneGeometry(const QSize &frame) const;
    /// Where each face of a pass is, in the order given. Each face is matched
    /// to its body (by name, otherwise by the body containing the face) for
    /// the ankles.
    QList<Kerfur::Placement> placeFaces(const Kerfur::SceneGeometry &geometry,
                                        const QList<Kerfur::AnalysedFace> &faces,
                                        const QList<Kerfur::AnalysedPerson> &people) const;
    void setGaze(const Kerfur::GazeAngles &gaze);
    /// Stores one threshold and hands it to the worker, queued onto its
    /// thread with the value.
    void setThreshold(qreal &stored, qreal value, Kerfur::VisionThreshold which);

    /// The throttle interval of the mode in use.
    int frameInterval() const;
    /// A pass found somebody, or did not.
    void notePresence(bool present);
    /// The idle clock has started watching.
    void startWatching();
    /// Hands the idle settings to the clock.
    void applyIdleLimits();
    /// `probe` gives the models `probeSeconds` to find somebody rather than
    /// `idleAfterSeconds`.
    void stopWatching(bool probe);
    /// One watched frame: true when it changed enough to wake the models.
    /// Takes the first thumbnail after a start as the reference.
    bool motionWakes(const QImage &thumbnail);
    /// Shows a watched frame, when a view is showing frames at all.
    void showWatchedFrame(const QImage &captured);

    bool m_enabled{false};
    bool m_faceDetected{false};
    bool m_following{false};
    QPointF m_faceCenter{0.5, 0.5};
    /// The point followed on the last pass, unsmoothed. Keeps the eyes on the
    /// same person when another of the same rank is about as near.
    QPointF m_attentionPoint{0.5, 0.5};
    QString m_errorString;
    QImage m_cameraFrame;

    QVariantList m_faces;
    QVariantList m_people;
    QVariantList m_faceLandmarks;
    QVariantList m_hands;
    QVariantList m_held;
    QVariantMap m_primaryFace;
    Kerfur::AnalysisCapabilities m_capabilities;
    QSize m_analysisSize;
    qint64 m_analysisMs{0};

    QCamera m_camera;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    QCameraDevice m_preferredCamera;
    QList<QCameraDevice> m_cameras;
    QStringList m_cameraLabels;
    QByteArray m_wantedCamera;
    /// The id of the camera the last open was for, cleared when it closes.
    QByteArray m_openCameraId;

    /// Probed once, outside a light session: a CSI camera is wired in rather
    /// than plugged in, and probing starts libcamera's camera manager.
    QList<Kerfur::LibcameraDevice> m_libcameras;
    bool m_libcamerasProbed{false};
    /// Index into `m_libcameras` of the chosen camera, or -1 when the choice
    /// is a QCamera device.
    qsizetype m_preferredLibcamera{-1};
    Kerfur::LibcameraCapture m_libcamera;
    /// The browser backend fills its device list after start-up, so a camera
    /// that did not exist when the tracker was enabled can still arrive.
    QMediaDevices m_mediaDevices;
    Kerfur::CapturePermission m_cameraPermission{Kerfur::CapturePermission::Device::Camera};
    QSize m_requestedResolution{1920, 1080};
    QSize m_captureResolution;

    QThread m_workerThread;
    Kerfur::FaceAnalysisWorker *m_worker{nullptr};
    bool m_analysisBusy{false};
    /// Whether the worker has finished loading and started answering. Until it
    /// has, the preview is published as it arrives rather than being held for a
    /// pass that is still queued behind the model load.
    bool m_analysisReady{false};
    quint64 m_sequence{0};
    quint64 m_peopleRevision{0};

    qreal m_detectionConfidence{0.6};
    qreal m_trackingSmoothing{0.0};
    qreal m_personConfidence{0.4};
    qreal m_keypointConfidence{0.4};
    qreal m_identityThreshold{0.363};
    qreal m_palmConfidence{0.55};
    qreal m_handConfidence{0.5};
    qreal m_objectConfidence{0.35};

    Kerfur::CameraMount m_mount;
    /// The setting; 0 means automatic. The mount holds the figure in use.
    qreal m_fieldOfView{0.0};
    Kerfur::GazeAngles m_gaze;
    bool m_hasGaze{false};
    qreal m_attentionDistance{-1.0};

    bool m_idleWatch{true};
    int m_idleAfterSeconds{30};
    qreal m_watchHz{2.0};
    qreal m_wakePercent{1.0};
    qreal m_probeSeconds{3.0};
    qreal m_motionPercent{0.0};
    /// The monotonic clock `m_idleClock` is given its times from.
    QElapsedTimer m_clock;
    Kerfur::IdleClock m_idleClock;
    Kerfur::MotionGate m_motionGate;
    int m_previewViewers{0};
    int m_analysisMaxSide{defaultAnalysisMaxSide};
    int m_previewMaxSide{defaultPreviewMaxSide};
    int m_updateHz{defaultUpdateHz};
    bool m_loneStrangerHands{true};
    bool m_expressionEnabled{false};

    /// The preview picture belonging to the pass that is still in flight, and
    /// the sequence number that says which pass that is. Held rather than shown
    /// so that the frame and the boxes measured on it reach the screen
    /// together.
    QImage m_pendingPreview;
    quint64 m_pendingPreviewSequence{0};

    QTimer m_frameThrottleTimer;
    QTimer m_cameraRetryTimer;
    /// Restarted by every start. When it fires the camera has either not
    /// started or started without a picture, and the error string says which.
    QTimer m_startWatchdog;

    /// Frames a second, by default: taken from the camera, put through every
    /// model and published. Six, because this is a robot's perception and not a
    /// video call. A face does not change expression, a hand does not change
    /// grip and a person does not cross a room inside a hundred and sixty
    /// milliseconds, and every pass past the rate at which those things happen
    /// is a whole inference chain spent to redraw the same answer.
    static constexpr int defaultUpdateHz{6};

    /// The bounds. One a second is the slowest rate at which the eyes still
    /// look like they are following somebody rather than noticing them; past
    /// fifteen the chain cannot keep up on any machine this runs on, and the
    /// throttle would simply be replaced by the analysis latch.
    static constexpr int minimumUpdateHz{1};
    static constexpr int maximumUpdateHz{15};

    /// The frame rate the capture format is chosen to cover. Well above the
    /// update rate, so that a frame is always waiting rather than being waited
    /// for, and far below the sixty a webcam will happily offer for nothing but
    /// bus bandwidth and heat.
    static constexpr float preferredCaptureFrameRate{30.0F};

    /// Milliseconds between two passes at a given rate.
    static constexpr int intervalForRate(int hertz)
    {
        return 1000 / std::clamp(hertz, minimumUpdateHz, maximumUpdateHz);
    }

    /// The longer side the analysis image is reduced to. Large enough that an
    /// iris is still a dozen pixels across at conversational distance, small
    /// enough that the colour conversion is not itself the cost. It is only the
    /// default: `analysisMaxSide` moves it, within the bounds below.
    static constexpr int defaultAnalysisMaxSide{960};

    /// Below the lower bound the face detector is looking at a thumbnail and
    /// reports nothing at all, which reads as a broken camera rather than as a
    /// setting. The upper bound is the longer side of the format the camera is
    /// asked for: the frame is never enlarged past what arrives, so every value
    /// above it would behave identically while still looking like a choice.
    static constexpr int minimumAnalysisMaxSide{320};
    static constexpr int maximumAnalysisMaxSide{1920};

    /// The longer side of the preview, by default. Large enough that the popup,
    /// which is most of the screen, is looking at real pixels rather than at a
    /// 640 wide picture stretched over 1200: that gap is what made the preview
    /// look like a camera fault rather than a preview setting.
    static constexpr int defaultPreviewMaxSide{960};

    /// The bounds. The floor is a picture still worth showing at the size the
    /// panel shows it; the ceiling is the largest format any camera here
    /// offers, and nothing above it can do anything, because the preview is
    /// never enlarged past the frame that arrived.
    static constexpr int minimumPreviewMaxSide{320};
    static constexpr int maximumPreviewMaxSide{1920};

    bool m_hasLastKnownPosition{false};
    bool m_isInitializing{false};
};
