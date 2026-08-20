// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "headpose.h"

#include <QLoggingCategory>
#include <QStringList>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(poseLog, "kerfur.vision.headpose")

// Beyond this the solve is reporting a head that has turned further than these
// five landmarks can still be seen on, which in practice means it has latched
// onto a mirrored or degenerate configuration.
constexpr double maximumPlausibleAngle{89.0};

// An iris window narrower than this carries too few pixels for the search to
// mean anything: at eleven pixels across, one eyelash moves the answer by a
// fifth of the range.
constexpr int minimumIrisWindow{11};

// The window is as tall as an eye and as wide as one, which is not the same
// number. A square window reaches the eyebrow and the upper rim of a pair of
// glasses, both of which are darker than an iris and neither of which moves
// when the eyes do.
constexpr double irisWindowAspect{0.5};

// The iris as a fraction of the window half width. The search is a matched
// filter for a dark disc of about this size rather than a centroid of dark
// pixels: a spectacle rim is as dark as an iris but it is a line, so it never
// fills a disc, and that is the whole difference between the two.
constexpr double irisDiscFraction{0.45};

// How much darker the disc must be than the ring immediately around it, in
// grey levels, before it is called an iris.
//
// Measured against the ring rather than against the window as a whole, and
// that is the whole reason this reports nothing instead of nonsense on a
// subject wearing glasses. A dark spectacle rim beats any threshold that only
// asks "is this darker than average"; what it cannot do is be dark while the
// ring around it is bright, because the rim is long and an iris is round.
constexpr double minimumIrisContrast{18.0};

// Outer radius of that ring, as a multiple of the disc. Far enough out to be
// sclera and skin, near enough to still be inside the eye.
constexpr double irisRingScale{2.0};

// How far the irises must sit from centre before the phrase mentions them, as
// a fraction of the eye half width, and how far the head must turn before the
// phrase mentions that. Both are the point where a human looking at the
// picture would agree rather than a threshold with a derivation.
constexpr double irisPhraseThreshold{0.22};
constexpr double yawPhraseThreshold{14.0};
constexpr double pitchPhraseThreshold{12.0};
constexpr double rollPhraseThreshold{12.0};

} // namespace

const std::array<cv::Point3f, faceLandmarkCount> &canonicalHeadModel()
{
    // y grows downwards, so the eyes are negative and the mouth positive. z
    // grows away from the camera, so both sit at positive z: the nose tip is
    // the origin and the nearest point on the face.
    //
    // These are not textbook anthropometry, and using textbook anthropometry
    // here is a mistake worth naming. The five points are wherever YuNet puts
    // them, and it puts its "nose tip" nearer the bridge than the tip and its
    // mouth corners wider than the vermilion. A model built from cadaver
    // measurements therefore has the eye line twice as far above the nose as
    // the detector reports it, and the solver absorbs the difference as
    // seventeen degrees of pitch on a subject looking straight ahead. What is
    // encoded here is the geometry of the landmarks, fitted so that a frontal
    // face reads as frontal.
    static const std::array<cv::Point3f, faceLandmarkCount> model{
        cv::Point3f{-32.0F, -36.0F, 34.0F}, // subject's right eye
        cv::Point3f{32.0F, -36.0F, 34.0F},  // subject's left eye
        cv::Point3f{0.0F, 0.0F, 0.0F},      // nose tip
        cv::Point3f{-27.0F, 18.0F, 18.0F},  // right mouth corner
        cv::Point3f{27.0F, 18.0F, 18.0F},   // left mouth corner
    };
    return model;
}

HeadPose estimateHeadPose(const std::array<cv::Point2f, faceLandmarkCount> &landmarks,
                          const cv::Size &imageSize)
{
    HeadPose pose;
    if (imageSize.width <= 0 || imageSize.height <= 0) {
        return pose;
    }

    // No calibration is available and none is worth demanding of the user, so
    // the focal length is approximated by the frame width. The approximation
    // costs accuracy in the absolute angles and almost nothing in their sign
    // and ordering, which is all the overlay and the behaviour engine read.
    const double focal{static_cast<double>(imageSize.width)};
    const cv::Matx33d camera{focal,
                             0.0,
                             imageSize.width / 2.0,
                             0.0,
                             focal,
                             imageSize.height / 2.0,
                             0.0,
                             0.0,
                             1.0};

    const std::array<cv::Point3f, faceLandmarkCount> &model{canonicalHeadModel()};
    const std::vector<cv::Point3f> objectPoints{model.cbegin(), model.cend()};
    const std::vector<cv::Point2f> imagePoints{landmarks.cbegin(), landmarks.cend()};

    try {
        cv::Vec3d rotation;
        cv::Vec3d translation;
        // SQPNP rather than the iterative solver: it is global, it needs no
        // initial guess, and it is defined for exactly this case of a handful
        // of near coplanar points where the iterative solver converges to
        // whichever of the two mirror solutions it started nearest.
        if (!cv::solvePnP(objectPoints,
                          imagePoints,
                          camera,
                          cv::noArray(),
                          rotation,
                          translation,
                          false,
                          cv::SOLVEPNP_SQPNP)) {
            return pose;
        }

        cv::Matx33d matrix;
        cv::Rodrigues(rotation, matrix);

        cv::Matx33d unusedQ;
        cv::Matx33d unusedR;
        // Euler angles of the rotation that takes the model into the camera,
        // in the order OpenCV composes them: about x, then y, then z. A head
        // looking straight down the lens is the identity, which is what the
        // model frame was chosen to make true.
        const cv::Vec3d euler{cv::RQDecomp3x3(matrix, unusedR, unusedQ)};

        // Reported from the subject's own point of view rather than the
        // image's, because that is the only description that does not change
        // meaning when the preview is mirrored: a person who turned right
        // turned right whichever way round the picture is drawn.
        pose.pitch = -euler[0];
        pose.yaw = euler[1];
        pose.roll = -euler[2];
        pose.valid = std::abs(pose.yaw) < maximumPlausibleAngle
                     && std::abs(pose.pitch) < maximumPlausibleAngle
                     && std::abs(pose.roll) < maximumPlausibleAngle;
    } catch (const cv::Exception &error) {
        qCWarning(poseLog) << "pose solve failed" << error.what();
        return HeadPose{};
    }
    return pose;
}

EyeGaze estimateIris(const cv::Mat &gray, const cv::Point2f &eyeCentre, float radius)
{
    EyeGaze gaze;
    if (gray.empty() || gray.type() != CV_8UC1 || radius <= 0.0F) {
        return gaze;
    }

    const double halfWidth{radius};
    const double halfHeight{radius * irisWindowAspect};
    const cv::Rect requested{static_cast<int>(std::lround(eyeCentre.x - halfWidth)),
                             static_cast<int>(std::lround(eyeCentre.y - halfHeight)),
                             static_cast<int>(std::lround(halfWidth * 2.0)),
                             static_cast<int>(std::lround(halfHeight * 2.0))};
    const cv::Rect window{requested & cv::Rect{0, 0, gray.cols, gray.rows}};

    // The clip must not have moved the window, only trimmed it: an eye against
    // the edge of the frame would otherwise be measured against a centre that
    // is no longer where the eye is.
    if (window != requested || window.width < minimumIrisWindow
        || window.height < minimumIrisWindow) {
        return gaze;
    }

    const int discRadius{
        std::max(1, static_cast<int>(std::lround(halfHeight * irisDiscFraction)))};
    const int kernelSide{(discRadius * 2) + 1};
    if (kernelSide >= window.height) {
        return gaze;
    }

    const int ringRadius{static_cast<int>(std::lround(discRadius * irisRingScale))};
    const int ringSide{(ringRadius * 2) + 1};
    if (ringSide >= window.height || ringSide >= window.width) {
        return gaze;
    }

    // Two normalized kernels over one padded window: the mean grey of an iris
    // sized disc, and the mean grey of the annulus around it. Their difference
    // at a pixel is how much an iris there would stand out from its own eye.
    cv::Mat disc{cv::Mat::zeros(ringSide, ringSide, CV_32F)};
    cv::circle(disc, cv::Point{ringRadius, ringRadius}, discRadius, cv::Scalar::all(1.0), -1);
    cv::Mat ring{cv::Mat::zeros(ringSide, ringSide, CV_32F)};
    cv::circle(ring, cv::Point{ringRadius, ringRadius}, ringRadius, cv::Scalar::all(1.0), -1);
    ring -= disc;
    const double discWeight{cv::sum(disc)[0]};
    const double ringWeight{cv::sum(ring)[0]};
    if (discWeight <= 0.0 || ringWeight <= 0.0) {
        return gaze;
    }
    disc /= discWeight;
    ring /= ringWeight;

    cv::Mat patch;
    gray(window).convertTo(patch, CV_32F);

    cv::Mat discMeans;
    cv::Mat ringMeans;
    cv::filter2D(patch, discMeans, CV_32F, disc, cv::Point{-1, -1}, 0.0, cv::BORDER_REPLICATE);
    cv::filter2D(patch, ringMeans, CV_32F, ring, cv::Point{-1, -1}, 0.0, cv::BORDER_REPLICATE);

    // The search is restricted to the interior: a kernel that hangs over the
    // edge of the window is averaging invented pixels, and invented pixels are
    // exactly as dark as the code that invented them.
    const cv::Rect interior{ringRadius,
                            ringRadius,
                            window.width - (ringRadius * 2),
                            window.height - (ringRadius * 2)};
    if (interior.width <= 0 || interior.height <= 0) {
        return gaze;
    }

    const cv::Mat response{ringMeans(interior) - discMeans(interior)};
    double best{0.0};
    cv::Point centre;
    cv::minMaxLoc(response, nullptr, &best, nullptr, &centre);
    if (best < minimumIrisContrast) {
        return gaze;
    }

    // A peak sitting on the edge of the search region means the real one is
    // outside the window: an eyebrow, a hairline, or the frame of a pair of
    // glasses. The measurement is not of an iris, and reporting it as one is
    // worse than reporting nothing.
    if (interior.width > 2 && interior.height > 2
        && (centre.x == 0 || centre.y == 0 || centre.x == interior.width - 1
            || centre.y == interior.height - 1)) {
        return gaze;
    }
    centre += interior.tl();

    gaze.offset = QPointF{
        std::clamp((centre.x - (window.width / 2.0)) / (window.width / 2.0), -1.0, 1.0),
        std::clamp((centre.y - (window.height / 2.0)) / (window.height / 2.0), -1.0, 1.0)};
    gaze.valid = true;
    return gaze;
}

QString describeGaze(const HeadPose &pose, const EyeGaze &rightEye, const EyeGaze &leftEye)
{
    if (!pose.valid) {
        return {};
    }

    QStringList parts;

    if (pose.yaw > yawPhraseThreshold) {
        parts.append(QStringLiteral("turned right"));
    } else if (pose.yaw < -yawPhraseThreshold) {
        parts.append(QStringLiteral("turned left"));
    }

    if (pose.pitch > pitchPhraseThreshold) {
        parts.append(QStringLiteral("chin up"));
    } else if (pose.pitch < -pitchPhraseThreshold) {
        parts.append(QStringLiteral("chin down"));
    }

    if (std::abs(pose.roll) > rollPhraseThreshold) {
        parts.append(QStringLiteral("tilted"));
    }

    // Both irises, agreeing, or nothing. A person has two eyes and they point
    // the same way, so a single measurement is unsupported and two that
    // disagree mean at least one of them found something that was not an iris.
    // The alternative is a phrase that announces a glance nobody made, which is
    // a worse answer than saying only where the head is pointing.
    if (rightEye.valid && leftEye.valid) {
        const double right{rightEye.offset.x()};
        const double left{leftEye.offset.x()};
        if (right > irisPhraseThreshold && left > irisPhraseThreshold) {
            parts.append(QStringLiteral("eyes right"));
        } else if (right < -irisPhraseThreshold && left < -irisPhraseThreshold) {
            parts.append(QStringLiteral("eyes left"));
        }
    }

    if (parts.isEmpty()) {
        return QStringLiteral("at the camera");
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace Kerfur
