// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "orientationresolver.h"

#include <algorithm>
#include <vector>

namespace Kerfur {

namespace {

// Nose against the mean of both hips. Section 5.2 specifies the median across
// detections, which this takes per detection first so that one person standing
// on their hands cannot outvote everybody else through a shared median.
constexpr size_t noseIndex{static_cast<size_t>(Keypoint::Nose)};
constexpr size_t leftHipIndex{static_cast<size_t>(Keypoint::LeftHip)};
constexpr size_t rightHipIndex{static_cast<size_t>(Keypoint::RightHip)};

} // namespace

OrientationResolver::OrientationResolver() = default;

OrientationResolver::OrientationResolver(Config config) : m_config{config} {}

void OrientationResolver::setManualOverride(int degrees)
{
    m_manualDegrees = degrees < 0 ? -1 : ((degrees % 360) + 360) % 360;
    m_invertedRun = 0;
}

int OrientationResolver::degrees() const
{
    return m_manualDegrees >= 0 ? m_manualDegrees : m_automaticDegrees;
}

void OrientationResolver::reset()
{
    m_automaticDegrees = 0;
    m_invertedRun = 0;
    m_lastDetectionMs = 0;
}

int OrientationResolver::observe(const QList<PersonDetection> &detections, qint64 timestampMs)
{
    if (m_manualDegrees >= 0) {
        return m_manualDegrees;
    }

    if (!detections.isEmpty()) {
        m_lastDetectionMs = timestampMs;

        if (looksInverted(detections, m_config.minimumConfidence)) {
            ++m_invertedRun;
            if (m_invertedRun >= m_config.inversionRunLength) {
                m_automaticDegrees = (m_automaticDegrees + 180) % 360;
                m_invertedRun = 0;
            }
        } else {
            m_invertedRun = 0;
        }
        return m_automaticDegrees;
    }

    // Nobody in frame. The first call establishes the clock rather than
    // immediately counting as a blind interval, otherwise a fresh start with an
    // empty room flips the camera before it has looked at anything.
    if (m_lastDetectionMs == 0) {
        m_lastDetectionMs = timestampMs;
        return m_automaticDegrees;
    }

    if ((timestampMs - m_lastDetectionMs) >= m_config.blindProbeIntervalMs) {
        m_automaticDegrees = (m_automaticDegrees + 180) % 360;
        m_lastDetectionMs = timestampMs;
        m_invertedRun = 0;
    }
    return m_automaticDegrees;
}

bool OrientationResolver::looksInverted(const QList<PersonDetection> &detections,
                                        float minimumConfidence)
{
    std::vector<float> verdicts;
    verdicts.reserve(static_cast<size_t>(detections.size()));

    for (const PersonDetection &detection : detections) {
        const Keypoint2D &nose{detection.keypoints[noseIndex]};
        const Keypoint2D &leftHip{detection.keypoints[leftHipIndex]};
        const Keypoint2D &rightHip{detection.keypoints[rightHipIndex]};
        if (nose.confidence < minimumConfidence) {
            continue;
        }

        float hipY{0.0F};
        int hips{0};
        if (leftHip.confidence >= minimumConfidence) {
            hipY += static_cast<float>(leftHip.position.y());
            ++hips;
        }
        if (rightHip.confidence >= minimumConfidence) {
            hipY += static_cast<float>(rightHip.position.y());
            ++hips;
        }
        if (hips == 0) {
            continue;
        }

        // Image y grows downwards, so an upright person has their nose above
        // their hips and therefore a smaller y.
        verdicts.push_back(static_cast<float>(nose.position.y()) - (hipY / hips));
    }

    if (verdicts.empty()) {
        return false;
    }

    const size_t middle{verdicts.size() / 2};
    std::nth_element(verdicts.begin(), verdicts.begin() + middle, verdicts.end());
    return verdicts[middle] > 0.0F;
}

} // namespace Kerfur
