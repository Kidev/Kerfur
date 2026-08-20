// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_FACEANALYSIS_H
#define KERFUR_FACEANALYSIS_H

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
};

/// One person the pose stage found, with the seventeen COCO keypoints.
struct AnalysedPerson
{
    QRectF box;
    QList<QPointF> keypoints;
    QList<double> keypointScores;
    double score{0.0};
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
    QString galleryDirectory;
    QStringList enrolled;
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
