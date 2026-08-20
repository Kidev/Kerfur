// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_FACEANALYSISWORKER_H
#define KERFUR_FACEANALYSISWORKER_H

#include "faceanalysis.h"
#include "facepipeline.h"
#include "handlandmarker.h"
#include "identityregistry.h"
#include "identitystore.h"
#include "objectdetector.h"
#include "posedetector.h"

#include <QImage>
#include <QObject>

#include <opencv2/core.hpp>

#include <array>

namespace Kerfur {

/// Every model the camera view runs, on one thread that is not the GUI thread.
///
/// The stages themselves are the ones the CPU perception backend already owns;
/// what this adds is the part a live preview needs and a headless pipeline does
/// not: the landmarks, the head rotation and the iris offsets, all reported
/// against the frame they were measured in. Running them here rather than in
/// the video frame handler is not an optimization. A pose pass is tens of
/// milliseconds and a face pass tens more, and on the GUI thread that is the
/// whole frame budget spent before a single pixel has been drawn.
class FaceAnalysisWorker : public QObject
{
    Q_OBJECT

public:
    explicit FaceAnalysisWorker(QObject *parent = nullptr);

    /// Frames analysed between two pose passes. The skeleton is the expensive
    /// stage and a person does not change shape between frames; the faces are
    /// found every frame regardless, because the eyes are what the view is
    /// about.
    static constexpr int poseStride{2};

    /// Faces analysed in one pass. Past this the pass costs more than the
    /// interval between frames and the view falls behind the room.
    static constexpr int maxFaces{3};

    /// Identity observations kept per face before the name is considered
    /// settled. One frame's cosine match against a gallery flickers between the
    /// nearest two people whenever the head turns; a short vote does not.
    static constexpr int identityVotes{5};

    /// Frames between two object passes. COCO is a second whole network and a
    /// held mug does not become a held phone between frames. It also only runs
    /// at all when somebody recognised has a hand in view, so this stride is
    /// the cadence of the rarer case rather than of every frame.
    static constexpr int objectStride{4};

public slots:
    /// Loads every model the catalog has a file for and enrols the gallery.
    /// Emits `ready` in all cases, including the one where nothing loaded, so
    /// that the view always learns why it is not seeing anything.
    void initialize();

    /// Runs one pass. `image` must be in the same orientation the view will
    /// draw, and `mirrored` must say whether that orientation is a reflection
    /// of the camera's own, because a reflection has to be undone before the
    /// head rotation is solved and must not be undone anywhere else.
    void analyse(const QImage &image, quint64 sequence, bool mirrored);

    void setFaceConfidence(double confidence);

signals:
    void ready(const Kerfur::AnalysisCapabilities &capabilities);
    void resultReady(const Kerfur::AnalysisResult &result);

private:
    /// One face followed across frames, purely so that its name can be voted on
    /// rather than recomputed from scratch every pass.
    struct FaceTrack
    {
        cv::Rect2f box;
        std::array<QString, identityVotes> votes;
        int voteCursor{0};
        int voteCount{0};
        quint64 lastSeen{0};
        QString settled;
        double settledConfidence{0.0};
    };

    AnalysedFace describeFace(const cv::Mat &bgr,
                              const cv::Mat &gray,
                              const FaceObservation &observation,
                              bool mirrored);

    /// Copies each face's name onto the person whose head region contains it,
    /// then locates the hands of whoever ended up with one.
    ///
    /// `mirrored` says whether the frame is a reflection, and it decides which
    /// of the pose model's two arms is the subject's left one.
    void attributePeople(AnalysisResult &result, bool mirrored) const;

    /// Replaces the forearm estimate with measured landmarks, for as many
    /// hands as the per pass budget allows, highest rank and nearest first.
    void refineHands(const cv::Mat &bgr, AnalysisResult &result);

    /// Runs the COCO stage and hangs whatever it found on the hand it overlaps.
    /// Does nothing, and does not run the network at all, when no recognised
    /// person has a hand in view.
    void attributeHeldObjects(const cv::Mat &bgr, AnalysisResult &result);

    /// A number that changes whenever anything a consumer draws about the
    /// people changed. The pose stage is on a stride and the previous skeleton
    /// stands between passes, but the names and the held objects are recomputed
    /// every frame, so the pose revision alone would publish a body without its
    /// label and then never correct it.
    static quint64 fingerprint(const QList<AnalysedPerson> &people);

    QString voteIdentity(const cv::Rect2f &box,
                         const QString &observed,
                         double similarity,
                         quint64 sequence,
                         double &confidence);

    PoseDetector m_pose;
    FacePipeline m_faces;
    IdentityStore m_identities;
    ObjectDetector m_objects;
    HandLandmarker m_handLandmarker;
    IdentityRegistry m_registry;

    QList<FaceTrack> m_tracks;
    QList<AnalysedPerson> m_lastPeople;
    cv::Size m_lastPoseFrame;

    /// The COCO stage's last answer, normalized, held between strides. The
    /// objects are cached and the association is not: a mug stays a mug for a
    /// second, but the hand around it moves every frame.
    QList<HeldObject> m_lastObjects;
    cv::Size m_lastObjectFrame;

    quint64 m_passes{0};
    quint64 m_peopleRevision{0};
    quint64 m_peopleFingerprint{0};
    double m_faceConfidence{0.6};
    bool m_initialized{false};
};

/// A square crop around whichever of the head keypoints the pose stage was
/// confident about, falling back to the top of the person box when it was
/// confident about none of them.
///
/// Free standing so that it can be checked against hand built keypoints, which
/// is the only part of the association between a body and a face that is pure
/// arithmetic.
cv::Rect headRegionFromKeypoints(const AnalysedPerson &person,
                                 const cv::Size &frameSize,
                                 float minimumConfidence);

/// The hand beyond one wrist, extrapolated along the forearm.
///
/// `wrist` and `elbow` are COCO keypoint indices and `side` is the subject's
/// own. Returns an invalid hand when the wrist keypoint is below
/// `minimumConfidence`, because a hand Kerfur cannot see is not a hand it
/// should draw a box around.
///
/// Free standing for the same reason `headRegionFromKeypoints` is: it is pure
/// arithmetic on keypoints and can be checked against hand built ones.
AnalysedHand handFromKeypoints(const AnalysedPerson &person,
                               Keypoint wrist,
                               Keypoint elbow,
                               const QString &side,
                               const QSize &frameSize,
                               float minimumConfidence);

/// How much of `hand` lies inside `object`, as a fraction of the hand's area.
///
/// Not intersection over union: a hand is small and a held guitar is not, so
/// the union is dominated by the object and a real hold scores near zero.
double handInsideObject(const QRectF &hand, const QRectF &object);

} // namespace Kerfur

#endif // KERFUR_FACEANALYSISWORKER_H
