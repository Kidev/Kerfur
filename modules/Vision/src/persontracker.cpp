// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "persontracker.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Kerfur {

namespace {

// How many recent observations the identity vote holds, and how many of them
// have to agree before a track takes a name. Seven and four means a track needs
// a majority of about half a second of evidence, and loses the name only when
// the majority goes the other way.
constexpr size_t identityWindow{7};
constexpr size_t identityMajority{4};

// A keypoint below this is a guess the model is reporting for completeness, and
// including it in a distance makes two unrelated skeletons look similar.
constexpr float trackingKeypointConfidence{0.3F};

// Weight of the newest measurement in the velocity estimate. Low, because the
// velocity exists to bridge a missed frame or two and a responsive estimate
// would mostly track detector jitter.
constexpr float velocitySmoothing{0.35F};

} // namespace

void IdentityVote::observe(const QString &candidate, double similarity)
{
    m_window.push_back(candidate);
    if (m_window.size() > identityWindow) {
        m_window.pop_front();
    }
    if (!candidate.isEmpty()) {
        m_lastSimilarity = similarity;
    }

    // Count the most frequent non empty name in the window. An empty name is
    // never promoted: "not recognised" is the absence of a settled identity,
    // not an identity of its own.
    QString leader;
    size_t leaderCount{0};
    for (const QString &name : m_window) {
        if (name.isEmpty()) {
            continue;
        }
        const size_t count{
            static_cast<size_t>(std::count(m_window.cbegin(), m_window.cend(), name))};
        if (count > leaderCount) {
            leaderCount = count;
            leader = name;
        }
    }

    if (leaderCount >= identityMajority) {
        m_settled = leader;
        m_confidence = m_lastSimilarity;
        return;
    }

    // Losing the name needs the same majority to have gone, not merely a single
    // disagreeing frame. Without this the identity sensor oscillates every time
    // somebody turns their head.
    if (!m_settled.isEmpty()) {
        const size_t settledCount{
            static_cast<size_t>(std::count(m_window.cbegin(), m_window.cend(), m_settled))};
        if (settledCount == 0) {
            m_settled.clear();
            m_confidence = 0.0;
        }
    }
}

PersonTracker::PersonTracker() = default;

PersonTracker::PersonTracker(Config config) : m_config{config} {}

void PersonTracker::reset()
{
    m_tracks.clear();
    m_nextId = 1;
}

void PersonTracker::update(const QList<PersonDetection> &detections, qint64 timestampMs)
{
    Q_UNUSED(timestampMs)

    for (PersonTrack &track : m_tracks) {
        track.matchedThisFrame = false;
        // Constant velocity prediction. The prediction is used for matching
        // only and is overwritten by the measurement on a hit, so a wrong
        // prediction costs one frame of association quality rather than
        // dragging the reported box away from the person.
        track.box.x += track.velocity.x;
        track.box.y += track.velocity.y;
    }

    struct Candidate
    {
        float cost;
        size_t trackIndex;
        qsizetype detectionIndex;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(m_tracks.size() * static_cast<size_t>(detections.size()));

    for (size_t trackIndex{0}; trackIndex < m_tracks.size(); ++trackIndex) {
        const PersonTrack &track{m_tracks[trackIndex]};
        for (qsizetype detectionIndex{0}; detectionIndex < detections.size(); ++detectionIndex) {
            const PersonDetection &detection{detections.at(detectionIndex)};
            const float overlap{intersectionOverUnion(track.box, detection.box)};
            if (overlap < m_config.minimumIou) {
                continue;
            }
            const float skeleton{
                keypointDistance(track.keypoints, detection.keypoints, detection.box)};
            const float cost{(1.0F - overlap) + (m_config.keypointWeight * skeleton)};
            candidates.push_back(Candidate{cost, trackIndex, detectionIndex});
        }
    }

    // Greedy rather than Hungarian. With the handful of people a robot face
    // ever sees at once, the optimal assignment and the greedy one differ only
    // in cases the gating threshold has already thrown out.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
        return left.cost < right.cost;
    });

    std::vector<bool> trackTaken(m_tracks.size(), false);
    std::vector<bool> detectionTaken(static_cast<size_t>(detections.size()), false);

    for (const Candidate &candidate : candidates) {
        if (trackTaken[candidate.trackIndex]
            || detectionTaken[static_cast<size_t>(candidate.detectionIndex)]) {
            continue;
        }
        trackTaken[candidate.trackIndex] = true;
        detectionTaken[static_cast<size_t>(candidate.detectionIndex)] = true;

        PersonTrack &track{m_tracks[candidate.trackIndex]};
        const PersonDetection &detection{detections.at(candidate.detectionIndex)};

        const cv::Point2f previousCentre{track.box.x + (track.box.width / 2.0F),
                                         track.box.y + (track.box.height / 2.0F)};
        const cv::Point2f measuredCentre{detection.box.x + (detection.box.width / 2.0F),
                                         detection.box.y + (detection.box.height / 2.0F)};
        const cv::Point2f delta{measuredCentre - previousCentre};
        track.velocity = (track.velocity * (1.0F - velocitySmoothing))
                         + (delta * velocitySmoothing);

        track.box = detection.box;
        track.keypoints = detection.keypoints;
        track.score = detection.score;
        track.matchedThisFrame = true;
        track.misses = 0;
        ++track.hits;
        if (track.hits >= m_config.minimumHits) {
            track.confirmed = true;
        }
    }

    for (qsizetype detectionIndex{0}; detectionIndex < detections.size(); ++detectionIndex) {
        if (detectionTaken[static_cast<size_t>(detectionIndex)]) {
            continue;
        }
        const PersonDetection &detection{detections.at(detectionIndex)};

        PersonTrack track;
        track.id = m_nextId++;
        track.box = detection.box;
        track.keypoints = detection.keypoints;
        track.score = detection.score;
        track.hits = 1;
        track.matchedThisFrame = true;
        track.confirmed = m_config.minimumHits <= 1;
        m_tracks.push_back(std::move(track));
    }

    for (PersonTrack &track : m_tracks) {
        if (!track.matchedThisFrame) {
            ++track.misses;
        }
    }

    const int maximumMisses{m_config.maximumMisses};
    m_tracks.erase(std::remove_if(m_tracks.begin(),
                                  m_tracks.end(),
                                  [maximumMisses](const PersonTrack &track) {
                                      return track.misses > maximumMisses;
                                  }),
                   m_tracks.end());
}

QList<const PersonTrack *> PersonTracker::confirmedTracks() const
{
    QList<const PersonTrack *> confirmed;
    confirmed.reserve(static_cast<qsizetype>(m_tracks.size()));
    for (const PersonTrack &track : m_tracks) {
        if (track.confirmed) {
            confirmed.append(&track);
        }
    }
    return confirmed;
}

float intersectionOverUnion(const cv::Rect2f &left, const cv::Rect2f &right)
{
    if (left.width <= 0.0F || left.height <= 0.0F || right.width <= 0.0F
        || right.height <= 0.0F) {
        return 0.0F;
    }
    const cv::Rect2f overlap{left & right};
    const float intersection{overlap.width * overlap.height};
    const float united{(left.width * left.height) + (right.width * right.height) - intersection};
    if (united <= 0.0F) {
        return 0.0F;
    }
    return intersection / united;
}

float keypointDistance(const std::array<Keypoint2D, keypointCount> &left,
                       const std::array<Keypoint2D, keypointCount> &right,
                       const cv::Rect2f &reference)
{
    const float diagonal{
        std::sqrt((reference.width * reference.width) + (reference.height * reference.height))};
    if (diagonal <= 0.0F) {
        return 1.0F;
    }

    float total{0.0F};
    int counted{0};
    for (size_t index{0}; index < left.size(); ++index) {
        if (left[index].confidence < trackingKeypointConfidence
            || right[index].confidence < trackingKeypointConfidence) {
            continue;
        }
        const QPointF delta{left[index].position - right[index].position};
        total += static_cast<float>(std::hypot(delta.x(), delta.y()));
        ++counted;
    }

    if (counted == 0) {
        return 1.0F;
    }
    return std::min(1.0F, (total / static_cast<float>(counted)) / diagonal);
}

} // namespace Kerfur
