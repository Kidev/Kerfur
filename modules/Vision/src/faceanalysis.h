// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_FACEANALYSIS_H
#define KERFUR_FACEANALYSIS_H

#include "identityregistry.h"

#include <QList>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

namespace Kerfur {

/// What one pass of the analysis worker concluded, in the vocabulary the camera
/// view needs.
///
/// Every geometry here is normalized to the analysed frame rather than given in
/// pixels. The view scales one image to a widget and another to a popup, the
/// analysis runs at a third resolution, and the preview JPEG at a fourth; a
/// pixel in this file would have to say which of the four it meant, and the
/// version of this code that did not say it drew the landmarks transposed for
/// two years.

/// The rotation of the head, in degrees, recovered from the five face
/// landmarks.
///
/// Signs are stated from the subject's own point of view, not the image's: yaw
/// is positive when they turn to their own right, pitch when their chin rises,
/// roll when they tip their head towards their own right shoulder. Said the
/// other way round, every sign would flip when the preview is mirrored, and the
/// preview is mirrored.
struct HeadPose
{
    double yaw{0.0};
    double pitch{0.0};
    double roll{0.0};
    bool valid{false};
};

/// Where the iris sits inside its own eye, as a fraction of the eye's half
/// width. Zero is centred and positive is towards the subject's own right, to
/// agree with HeadPose; y grows downwards, so positive is a lowered gaze.
///
/// This is deliberately not called a gaze angle. Without a calibrated eye
/// model, an iris offset is an ordinal quantity: it says which way the eyes
/// have moved relative to the head, not where in the room they point.
struct EyeGaze
{
    QPointF offset;
    bool valid{false};
};

/// One face, with whatever the loaded models could say about it. A stage that
/// is not provisioned leaves its own fields empty rather than failing the
/// whole observation, which is what lets a build with three of the five model
/// files still draw a box and a name.
struct AnalysedFace
{
    QRectF box;
    /// YuNet order: the subject's right eye, left eye, nose tip, right mouth
    /// corner, left mouth corner.
    QList<QPointF> landmarks;
    QString identity;
    double identityConfidence{0.0};
    QString emotion;
    double emotionConfidence{0.0};
    HeadPose pose;
    EyeGaze rightEye;
    EyeGaze leftEye;
    /// A short human reading of pose and irises together, for the overlay.
    QString gazeDirection;
    double score{0.0};

    /// The rank `identities/ids.json` gives this person, or
    /// `IdentityRegistry::unknownPriority` for a stranger. Zero is the top, and
    /// it is what decides whose face the rendered eyes follow when more than
    /// one person is in the room.
    int priority{IdentityRegistry::unknownPriority};
};

/// One hand.
///
/// This is reported at one of two qualities and says which. The skeleton always
/// gives a coarse answer: the pose stage finds a wrist and the elbow behind it,
/// and a hand sits roughly a third of a forearm beyond the wrist along that
/// line, which is enough to say where to look. When the hand models are
/// provisioned that region is then handed to MediaPipe's palm detector and
/// landmark model, and the box, the landmarks and the grip below are measured
/// rather than extrapolated. Consumers must not assume the good case: the
/// models are optional, the person may be too far away, and a hand behind a
/// back has a wrist estimate and nothing else.
struct AnalysedHand
{
    /// Normalized, like everything else here.
    QRectF box;
    QPointF wrist;
    /// The pose stage's confidence in the wrist keypoint, or the landmark
    /// model's confidence in the hand once it has run. A hand is reported only
    /// above the relevant floor, so this is never noise, but it is still worth
    /// carrying: a hand at 0.42 and one at 0.95 want to be drawn differently.
    double confidence{0.0};
    /// The subject's own side, "left" or "right". Taken from the skeleton,
    /// which knows which arm this is, and not from the landmark model's own
    /// handedness output, which is a guess made from one cropped hand.
    QString side;
    bool valid{false};

    /// The twenty-one MediaPipe points, normalized. Empty when only the
    /// skeleton estimate is available, which is what `tracked` reports.
    QList<QPointF> landmarks;
    /// Zero for a flat open hand, one for a closed fist. Zero when untracked,
    /// which is why `tracked` has to be consulted before believing it.
    double grip{0.0};
    /// True when the landmark model produced this hand, false when it is the
    /// forearm extrapolation.
    bool tracked{false};
};

/// Something a known person is holding.
///
/// Reported only when it overlaps a hand. An object lying on the desk in front
/// of somebody is a thing in the room, not a thing in their hand, and the
/// difference is the entire point of the association.
struct HeldObject
{
    QRectF box;
    QString label;
    double score{0.0};
    /// Which hand it was associated with.
    QString side;
};

/// One person the pose stage found, with the seventeen COCO keypoints.
///
/// The identity is copied down from whichever face was found inside this
/// person's head region, and it is what gates the two expensive stages: hands
/// are only located for a recognised person, and objects are only looked for in
/// the hands of one. That is not an optimization dressed up as a rule. A camera
/// that reads what strangers are carrying is a different device from one that
/// notices its owner is holding a mug, and this is the line between them.
struct AnalysedPerson
{
    QRectF box;
    QList<QPointF> keypoints;
    QList<double> keypointScores;
    double score{0.0};

    QString identity;
    double identityConfidence{0.0};
    int priority{IdentityRegistry::unknownPriority};
    bool known{false};

    AnalysedHand leftHand;
    AnalysedHand rightHand;
    QList<HeldObject> held;
};

/// One worker pass. Carries the frame size it was computed against so that a
/// consumer can undo the normalization when it genuinely needs pixels.
struct AnalysisResult
{
    QSize frameSize;
    QList<AnalysedFace> faces;
    QList<AnalysedPerson> people;
    quint64 sequence{0};

    /// Bumped only when the pose stage actually ran. The stage is on a stride
    /// and the previous answer stands between passes, so without this the view
    /// would rebuild an identical skeleton ten times a second.
    quint64 peopleRevision{0};

    qint64 elapsedMs{0};
};

/// Which stages the worker actually loaded, and who it knows.
///
/// Reported once at startup rather than inferred from empty fields in a result,
/// because "the expression model is not provisioned" and "this face has no
/// expression the model recognises" produce the same empty string and want
/// entirely different reactions from whoever is reading the overlay.
struct AnalysisCapabilities
{
    bool faceReady{false};
    bool identityReady{false};
    bool emotionReady{false};
    bool poseReady{false};
    bool objectsReady{false};
    /// Both hand models loaded. Either one alone is useless, so this is one
    /// flag rather than two.
    bool handsReady{false};
    QString galleryDirectory;
    QStringList enrolled;
    /// The declared people, in rank order, as `id (rank)`. For the panel, so
    /// that a manifest whose ranks are not what the author meant is visible
    /// without opening the file.
    QStringList roster;
    /// Empty when everything loaded; otherwise a sentence naming what did not
    /// and where it was looked for.
    QString problem;
};

/// Registers the analysis types so that they can cross a queued connection.
/// Idempotent, and called by the tracker before it connects to the worker.
void ensureFaceAnalysisMetaTypesRegistered();

} // namespace Kerfur

Q_DECLARE_METATYPE(Kerfur::AnalysisResult)
Q_DECLARE_METATYPE(Kerfur::AnalysisCapabilities)

#endif // KERFUR_FACEANALYSIS_H
