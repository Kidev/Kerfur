// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_HEADPOSE_H
#define KERFUR_HEADPOSE_H

#include "faceanalysis.h"

#include <opencv2/core.hpp>

#include <array>

namespace Kerfur {

inline constexpr int faceLandmarkCount{5};

/// The generic head the pose is measured against, in the same order and the
/// same handedness as a YuNet detection: the subject's right eye, left eye,
/// nose tip, right mouth corner, left mouth corner.
///
/// Millimetres on an average adult skull, in a right handed frame whose x
/// grows towards the image right, y grows downwards to match image
/// coordinates, and z grows out of the face towards the camera. It is exposed
/// rather than hidden in the implementation so that a test can project it under
/// a known rotation and assert that the estimator recovers that rotation, which
/// is the only way to check a pose solver without a calibrated rig.
const std::array<cv::Point3f, faceLandmarkCount> &canonicalHeadModel();

/// Recovers the head rotation from five landmarks in `imageSize` pixels.
///
/// The landmarks must be in unmirrored camera space. A mirrored preview turns
/// the face into its own reflection, and a reflection is not a rotation: fed
/// one, a solver returns an improper matrix whose euler angles look plausible
/// and are wrong in the yaw sign, which is the failure that is impossible to
/// spot by looking at it.
///
/// Returns an invalid pose rather than a guess when the solve does not
/// converge or the landmarks are degenerate.
HeadPose estimateHeadPose(const std::array<cv::Point2f, faceLandmarkCount> &landmarks,
                          const cv::Size &imageSize);

/// Locates the iris inside one eye and reports how far it sits from the centre
/// of its own eye, as a fraction of the eye's half width.
///
/// `radius` is the half width of the window searched, which the caller derives
/// from the distance between the eyes so that the window scales with the face
/// rather than with the frame.
EyeGaze estimateIris(const cv::Mat &gray, const cv::Point2f &eyeCentre, float radius);

/// One short phrase covering where the head points and where the eyes sit
/// within it, for the overlay. Empty when the pose is not valid.
QString describeGaze(const HeadPose &pose, const EyeGaze &rightEye, const EyeGaze &leftEye);

} // namespace Kerfur

#endif // KERFUR_HEADPOSE_H
