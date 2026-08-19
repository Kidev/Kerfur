// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VISIONCONFIG_H
#define KERFUR_VISIONCONFIG_H

#include <QtGlobal>

namespace Kerfur {

/// What the CPU backend runs, and how often.
///
/// Everything here is a cadence or a threshold rather than a switch, because
/// the difference between a desktop and a browser is how much inference per
/// second is affordable, not which stages exist. Which stages exist is decided
/// by which model files were provisioned.
struct VisionConfig
{
    /// Captured frames to skip between processed ones. The camera delivers at
    /// its own rate and the pose model is the expensive part, so this is the
    /// coarse throttle.
    int frameStride{1};

    /// Processed frames between object detections. Held objects change on a
    /// human timescale and the COCO detector is a second full network, so it
    /// runs at a fraction of the pose cadence.
    int objectStride{3};

    /// Minimum gap between two face analyses of the same track. A face does not
    /// change identity, and its expression does not change faster than this.
    qint64 faceIntervalMs{400};

    /// Fraction of frame height a person must occupy before the face stages are
    /// worth running on them. Below it the crop is too small for the landmarks
    /// to align, so the result would be noise wearing a confidence value.
    double closeProximity{0.25};

    /// Face analyses allowed in one processed frame. Without a cap, a group
    /// photo turns one frame into a dozen sequential inferences and the
    /// pipeline stalls exactly when the most is happening.
    int faceAnalysesPerFrame{2};

    float poseConfidence{0.35F};
    float objectConfidence{0.35F};

    /// Keypoint confidence below which a point is not used for cropping or for
    /// the head centre.
    float keypointConfidence{0.4F};

    /// The profile for the platform this build targets. WebAssembly gets a
    /// leaner one: every frame there costs a CPU side copy out of a 2D canvas
    /// before inference has even started.
    static VisionConfig forPlatform()
    {
        VisionConfig config;
#ifdef Q_OS_WASM
        config.frameStride = 4;
        config.faceIntervalMs = 1000;
        config.faceAnalysesPerFrame = 1;
#endif
        return config;
    }
};

} // namespace Kerfur

#endif // KERFUR_VISIONCONFIG_H
