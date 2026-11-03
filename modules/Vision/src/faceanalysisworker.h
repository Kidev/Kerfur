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

#include <QHash>
#include <QImage>
#include <QObject>

#include <opencv2/core.hpp>

#include <array>
#include <optional>

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
    static constexpr int objectStride{2};

    /// Looks kept per hand before a held object's name is considered settled.
    /// Short, because a hold is an event with a beginning: a vote long enough
    /// to be certain is a vote that names the mug after it has been put down.
    static constexpr int heldVotes{3};

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

    /// Reads the galleries again and re-enrols, then emits `ready` with the
    /// new roster. Connected to `IdentityLibrary::galleryChanged`; a face
    /// whose file is unchanged is answered from the cache.
    void reloadGallery();

signals:
    void ready(const Kerfur::AnalysisCapabilities &capabilities);
    void resultReady(const Kerfur::AnalysisResult &result);

private:
    /// What one hand has been seen holding, and for how long it may be believed.
    ///
    /// The votes are labels rather than objects: what flickers between passes is
    /// the name, not the geometry, and a majority of names is the cheapest thing
    /// that turns "cup, bowl, cup" into "cup".
    struct HeldTrack
    {
        std::array<QString, heldVotes> votes;
        int cursor{0};
        int count{0};
        /// The most recent sighting whose name the vote agreed with, and the
        /// pass it was seen on. A sighting the vote does not back is counted
        /// and then dropped: it is evidence, not an answer.
        HeldObject held;
        quint64 heldAt{0};
        /// The last pass this hand was in view at all, which is what decides
        /// when the track itself is forgotten.
        quint64 lastSeen{0};

        void vote(const std::optional<HeldObject> &observed, quint64 pass);
        /// The remembered object, while the sighting behind it is recent enough
        /// to still be true.
        std::optional<HeldObject> settled(quint64 pass) const;
    };

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

    /// The gallery half of `initialize`, on top of the model half kept in
    /// `m_capabilities` and `m_modelProblems`.
    void enrolGallery();

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
    /// hands as the per pass budget allows, highest rank and nearest first, and
    /// moves the skeleton's wrist joint onto the palm base it measured.
    ///
    /// True when at least one hand was measured. The caller publishes a new
    /// revision when it was: the landmarks move every pass, and the pose
    /// stride's revision alone would draw them at half the rate of the picture
    /// they are supposed to sit on top of.
    bool refineHands(const cv::Mat &bgr, AnalysisResult &result);

    /// Runs the COCO stage over a crop around each hand that could be holding
    /// something, and hangs whatever it found on that hand. Does nothing, and
    /// does not run the network at all, when no recognised person has a hand in
    /// view.
    void attributeHeldObjects(const cv::Mat &bgr, AnalysisResult &result);

    /// The one object in a crop around `hand` that the hand is actually inside,
    /// in frame coordinates, or nothing. The best single object per hand rather
    /// than every overlapping one: a hand covered by three boxes is a hand
    /// holding one thing that three classes disagree about, and reporting all
    /// three reads as somebody juggling.
    std::optional<HeldObject> bestHeldObject(const cv::Mat &bgr,
                                             const AnalysedHand &hand,
                                             const cv::Size &frame);

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

    /// What the models said at start up. A gallery reload changes the roster
    /// and nothing else, so it starts from these.
    AnalysisCapabilities m_capabilities;
    QStringList m_modelProblems;

    QList<FaceTrack> m_tracks;
    QList<AnalysedPerson> m_lastPeople;
    cv::Size m_lastPoseFrame;

    /// What one hand of one person has been seen holding, over the last few
    /// looks. Keyed by name and side, because the association is what is being
    /// remembered: the object stage runs on a stride and its box is stale
    /// between passes, but the alternative is a label that flickers between the
    /// two nearest classes at the rate the network is run.
    QHash<QString, HeldTrack> m_held;

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

/// How big a hand on this body should be, in pixels of `frameSize`, judged from
/// the parts of it that are not the arm. Zero when nothing usable is visible.
///
/// The shoulders, the trunk, the eyes and the ears, each scaled by what a hand
/// measures against it. The largest reading wins rather than the first: every
/// one of these lengths shortens as the body turns away and none of them grows,
/// so the biggest is the one seen most nearly face on.
double bodyHandScale(const AnalysedPerson &person, const QSize &frameSize, float minimumConfidence);

/// The hand beyond one wrist, extrapolated along the forearm and sized by
/// `bodyHandScale` where the forearm alone would be misleading.
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

/// The crop the object stage is shown for one hand, in pixels of a frame of
/// `frameSize`. Empty when the hand is too small, or the frame too small, for a
/// crop to be worth taking.
///
/// Square, centred on the hand, and a few hand widths across: the detector has
/// one fixed input side, so a crop is the only way a held object arrives at it
/// large enough to be named rather than merely noticed.
cv::Rect objectRegionForHand(const AnalysedHand &hand, const cv::Size &frameSize);

/// How much of `hand` lies inside `object`, as a fraction of the hand's area.
///
/// Not intersection over union: a hand is small and a held guitar is not, so
/// the union is dominated by the object and a real hold scores near zero.
double handInsideObject(const QRectF &hand, const QRectF &object);

} // namespace Kerfur

#endif // KERFUR_FACEANALYSISWORKER_H
