// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ORIENTATIONRESOLVER_H
#define KERFUR_ORIENTATIONRESOLVER_H

#include "visiontypes.h"

#include <QList>

namespace Kerfur {

/// Decides how far the frame has to be rotated before inference.
///
/// The camera sometimes presents upside down, and section 5.2 of the design
/// makes recovering from that the detector's own job rather than a mounting
/// assumption baked into a build. Two mechanisms, because either alone has a
/// blind spot:
///
/// - the geometry check is precise but needs detections to work with,
/// - the blind probe covers the case where the inversion suppresses detection
///   well enough that the geometry check never receives any.
///
/// A manual override wins over both until it is returned to automatic, which is
/// what the control panel's orientation control sets.
class OrientationResolver
{
public:
    struct Config
    {
        /// Consecutive frames of inverted geometry before the flip. One frame
        /// of a person bending over reads exactly like an upside down camera,
        /// and a run of them does not.
        int inversionRunLength{8};

        /// How long the camera may deliver frames with nobody detected before
        /// the resolver tries the other way up.
        qint64 blindProbeIntervalMs{15000};

        /// Keypoints below this contribute nothing to the geometry check.
        float minimumConfidence{0.4F};
    };

    // See PersonTracker: a nested Config cannot supply a default argument to a
    // constructor of its own enclosing class.
    OrientationResolver();
    explicit OrientationResolver(Config config);

    /// Degrees, or a negative value to return to automatic.
    void setManualOverride(int degrees);
    bool isManual() const { return m_manualDegrees >= 0; }

    int degrees() const;

    /// Feeds one processed frame and returns the orientation that should be
    /// applied to the next one.
    int observe(const QList<PersonDetection> &detections, qint64 timestampMs);

    void reset();

private:
    static bool looksInverted(const QList<PersonDetection> &detections, float minimumConfidence);

    Config m_config;
    qint64 m_lastDetectionMs{0};
    int m_manualDegrees{-1};
    int m_automaticDegrees{0};
    int m_invertedRun{0};
};

} // namespace Kerfur

#endif // KERFUR_ORIENTATIONRESOLVER_H
