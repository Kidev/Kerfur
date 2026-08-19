// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PERSONTRACKER_H
#define KERFUR_PERSONTRACKER_H

#include "visiontypes.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <deque>
#include <vector>

namespace Kerfur {

/// Holds an identity steady across the frames where the recogniser disagrees
/// with itself.
///
/// A per frame identity flickers, because a face turning away produces one bad
/// embedding out of every few good ones. Every consumer of `identity` sees that
/// flicker as a person leaving and a different person arriving, Home Assistant
/// included, so the vote is held here rather than filtered downstream.
class IdentityVote
{
public:
    /// `candidate` may be empty, which is a vote for "not recognised" rather
    /// than an absence of evidence.
    void observe(const QString &candidate, double similarity);

    QString settled() const { return m_settled; }
    double confidence() const { return m_confidence; }

private:
    std::deque<QString> m_window;
    QString m_settled;
    double m_confidence{0.0};
    double m_lastSimilarity{0.0};
};

/// One person the tracker is following.
struct PersonTrack
{
    cv::Rect2f box;
    std::array<Keypoint2D, keypointCount> keypoints{};
    cv::Point2f velocity{};
    IdentityVote identity;
    QString emotion;
    QStringList heldObjects;
    qint64 lastFaceAnalysisMs{0};
    float score{0.0F};
    float emotionConfidence{0.0F};
    int id{-1};
    int hits{0};
    int misses{0};
    bool confirmed{false};
    bool matchedThisFrame{false};
};

/// Intersection over union combined with keypoint distance, greedy assignment,
/// constant velocity prediction, and birth and death counts.
///
/// Tracking is what makes identity and emotion stable, and it is what lets the
/// person count be a number rather than a flicker. Hailo ships a tracker, but
/// it lives in the GStreamer stack this design deliberately does not use, so
/// this one is shared by every backend.
class PersonTracker
{
public:
    struct Config
    {
        /// Frames a track must be seen before anything downstream hears about
        /// it. A single frame detection on a patterned background is common;
        /// three consecutive ones on the same spot are not.
        int minimumHits{3};

        /// Frames a track survives without a detection. Eight is about a third
        /// of a second at the desktop cadence, which covers a person turning
        /// side on without covering a person leaving.
        int maximumMisses{8};

        float minimumIou{0.2F};

        /// How much keypoint agreement counts against box agreement. Two people
        /// crossing have overlapping boxes and completely different skeletons,
        /// which is the case box overlap alone gets wrong.
        float keypointWeight{0.4F};
    };

    // Two constructors rather than a defaulted argument: a nested class's
    // default member initializers are not usable until the enclosing class is
    // complete, so `Config config = Config{}` here is ill-formed.
    PersonTracker();
    explicit PersonTracker(Config config);

    void update(const QList<PersonDetection> &detections, qint64 timestampMs);
    void reset();

    std::vector<PersonTrack> &tracks() { return m_tracks; }
    const std::vector<PersonTrack> &tracks() const { return m_tracks; }

    /// Confirmed tracks only, which is what a consumer should ever see.
    QList<const PersonTrack *> confirmedTracks() const;

private:
    Config m_config;
    std::vector<PersonTrack> m_tracks;
    int m_nextId{1};
};

/// Intersection over union of two boxes, zero when either is degenerate.
float intersectionOverUnion(const cv::Rect2f &left, const cv::Rect2f &right);

/// Mean distance between confident keypoint pairs, normalized by the diagonal
/// of `reference`. Returns 1.0 when the two share no confident keypoints, which
/// reads as maximum disagreement.
float keypointDistance(const std::array<Keypoint2D, keypointCount> &left,
                       const std::array<Keypoint2D, keypointCount> &right,
                       const cv::Rect2f &reference);

} // namespace Kerfur

#endif // KERFUR_PERSONTRACKER_H
