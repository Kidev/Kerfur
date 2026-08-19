// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"

namespace Kerfur {

namespace {

// Nose and both eyes. Ears are excluded on purpose: they are the keypoints a
// profile view keeps when the eyes are gone, and averaging one visible ear into
// the centre pulls the gaze target off the face rather than onto it.
constexpr Keypoint facialKeypoints[]{Keypoint::Nose, Keypoint::LeftEye, Keypoint::RightEye};

} // namespace

QPointF headCenterFromKeypoints(const QList<Keypoint2D> &keypoints, float minimumConfidence)
{
    if (keypoints.size() < keypointCount) {
        return {};
    }

    QPointF total;
    int counted{0};
    for (const Keypoint keypoint : facialKeypoints) {
        const Keypoint2D &candidate{keypoints.at(static_cast<int>(keypoint))};
        if (candidate.confidence < minimumConfidence) {
            continue;
        }
        total += candidate.position;
        ++counted;
    }

    if (counted == 0) {
        return {};
    }
    return total / counted;
}

void ensurePerceptionMetaTypesRegistered()
{
    // The templated no-argument form. Qt 6 removed the overload that took a
    // type name string, so passing "Kerfur::PerceptionFrame" here would not
    // compile.
    qRegisterMetaType<TrackedPerson>();
    qRegisterMetaType<PerceptionFrame>();
}

void TrackedPerson::setIdentity(const QString &identity, qreal confidence)
{
    m_identity = identity;
    m_identityConfidence = confidence;
}

void TrackedPerson::setEmotion(const QString &emotion, qreal confidence)
{
    m_emotion = emotion;
    m_emotionConfidence = confidence;
}

} // namespace Kerfur
