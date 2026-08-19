// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PERCEPTIONTYPES_H
#define KERFUR_PERCEPTIONTYPES_H

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// COCO keypoint indices, in the order the pose models emit them.
///
/// The numeric values are load bearing: P2 indexes model output directly with
/// them, and the upside down detection compares Nose against the hips, so this
/// order must not be rearranged for readability.
enum class Keypoint : int {
    Nose = 0,
    LeftEye,
    RightEye,
    LeftEar,
    RightEar,
    LeftShoulder,
    RightShoulder,
    LeftElbow,
    RightElbow,
    LeftWrist,
    RightWrist,
    LeftHip,
    RightHip,
    LeftKnee,
    RightKnee,
    LeftAnkle,
    RightAnkle
};

inline constexpr int keypointCount{17};

/// One keypoint: a position normalized to the frame, and how much the model
/// believes it.
///
/// The confidence travels with the position rather than being filtered out
/// upstream because different consumers need different floors. Gaze wants only
/// keypoints it can trust; the orientation check would rather have weak
/// evidence than none.
struct Keypoint2D
{
    QPointF position;
    float confidence{0.0F};
};

/// Registers the perception types for queued connections.
///
/// Q_GADGET already makes them known to QMetaType, so QVariant and the property
/// system need nothing. What a queued connection additionally needs is the name
/// lookup, and the documented contract is that it happens before the first such
/// connection is made. PerceptionPipe's constructor calls this, which is early
/// enough because the pipe is what every queued perception connection goes
/// through. Safe to call repeatedly.
void ensurePerceptionMetaTypesRegistered();

/// The mean of the confident facial keypoints, or a null point when none of
/// them clear `minimumConfidence`.
///
/// Returning null rather than a fallback is deliberate. A person facing away has
/// no detectable face, and an average of noise looks exactly like a real answer
/// to a caller that cannot tell the difference.
QPointF headCenterFromKeypoints(const QList<Keypoint2D> &keypoints, float minimumConfidence);

/// One person the tracker is following, as everything downstream sees them.
///
/// All geometry is normalized to the frame so that a consumer never has to know
/// the capture resolution, which differs between the two camera streams and
/// changes with orientation.
class TrackedPerson
{
    Q_GADGET
    QML_VALUE_TYPE(trackedPerson)

    Q_PROPERTY(int trackId READ trackId)
    Q_PROPERTY(QRectF boundingBox READ boundingBox)
    Q_PROPERTY(QPointF headCenter READ headCenter)
    Q_PROPERTY(qreal proximity READ proximity)
    Q_PROPERTY(QString identity READ identity)
    Q_PROPERTY(qreal identityConfidence READ identityConfidence)
    Q_PROPERTY(QString emotion READ emotion)
    Q_PROPERTY(qreal emotionConfidence READ emotionConfidence)
    Q_PROPERTY(QStringList heldObjects READ heldObjects)
    Q_PROPERTY(bool known READ isKnown)

public:
    int trackId() const { return m_trackId; }
    QRectF boundingBox() const { return m_boundingBox; }
    QPointF headCenter() const { return m_headCenter; }

    /// How much of the frame height this person occupies. This is what "close"
    /// means everywhere in Kerfur: it needs no camera calibration and it is
    /// monotonic in real distance for a fixed lens.
    qreal proximity() const { return m_proximity; }

    QString identity() const { return m_identity; }
    qreal identityConfidence() const { return m_identityConfidence; }
    QString emotion() const { return m_emotion; }
    qreal emotionConfidence() const { return m_emotionConfidence; }
    QStringList heldObjects() const { return m_heldObjects; }
    QList<Keypoint2D> keypoints() const { return m_keypoints; }

    bool isKnown() const { return !m_identity.isEmpty(); }

    void setTrackId(int trackId) { m_trackId = trackId; }
    void setBoundingBox(const QRectF &boundingBox) { m_boundingBox = boundingBox; }
    void setHeadCenter(const QPointF &headCenter) { m_headCenter = headCenter; }
    void setProximity(qreal proximity) { m_proximity = proximity; }
    void setIdentity(const QString &identity, qreal confidence);
    void setEmotion(const QString &emotion, qreal confidence);
    void setHeldObjects(const QStringList &heldObjects) { m_heldObjects = heldObjects; }
    void setKeypoints(const QList<Keypoint2D> &keypoints) { m_keypoints = keypoints; }

private:
    QRectF m_boundingBox;
    QPointF m_headCenter;
    QString m_identity;
    QString m_emotion;
    QStringList m_heldObjects;
    QList<Keypoint2D> m_keypoints;
    qreal m_proximity{0.0};
    qreal m_identityConfidence{0.0};
    qreal m_emotionConfidence{0.0};
    int m_trackId{-1};
};

/// Everything one processed camera frame concluded, as one value.
///
/// Producers hand exactly one of these across the thread boundary per frame.
/// The previous build emitted a separate signal per changed scalar, which meant
/// consumers saw torn state and the encoder property was recomputed several
/// times per frame.
class PerceptionFrame
{
    Q_GADGET
    QML_VALUE_TYPE(perceptionFrame)

    Q_PROPERTY(qint64 timestampMs READ timestampMs)
    Q_PROPERTY(int orientationDegrees READ orientationDegrees)

public:
    qint64 timestampMs() const { return m_timestampMs; }
    quint64 sequence() const { return m_sequence; }
    int orientationDegrees() const { return m_orientationDegrees; }
    QList<TrackedPerson> people() const { return m_people; }

    void setTimestampMs(qint64 timestampMs) { m_timestampMs = timestampMs; }
    void setSequence(quint64 sequence) { m_sequence = sequence; }
    void setOrientationDegrees(int degrees) { m_orientationDegrees = degrees; }
    void setPeople(const QList<TrackedPerson> &people) { m_people = people; }

private:
    QList<TrackedPerson> m_people;
    qint64 m_timestampMs{0};
    quint64 m_sequence{0};
    int m_orientationDegrees{0};
};

} // namespace Kerfur

#endif // KERFUR_PERCEPTIONTYPES_H
