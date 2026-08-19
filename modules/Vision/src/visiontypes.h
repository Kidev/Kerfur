// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VISIONTYPES_H
#define KERFUR_VISIONTYPES_H

#include "perceptiontypes.h"

#include <QString>

#include <opencv2/core.hpp>

#include <array>

namespace Kerfur {

/// One captured frame on its way into inference.
///
/// The image is a deep copy taken on the GUI thread, because a QVideoFrame's
/// mapped memory belongs to the capture backend and is unmapped the moment the
/// handler returns. Referencing it from a worker thread would be a use after
/// free that only shows up under load.
struct VisionFrame
{
    cv::Mat image;
    qint64 timestampMs{0};
    quint64 sequence{0};
};

/// One person as the pose stage saw them, before the tracker gives them an
/// identity that persists across frames.
///
/// Geometry is in source frame pixels rather than normalized, because every
/// consumer inside the backend needs pixels: the crops that feed the face
/// stages, the intersection over union in the tracker, and the wrist to object
/// overlap test. Normalization happens once, at the boundary where the
/// PerceptionFrame is built.
struct PersonDetection
{
    cv::Rect2f box;
    std::array<Keypoint2D, keypointCount> keypoints{};
    float score{0.0F};
};

/// One thing the COCO detector recognised.
struct ObjectDetection
{
    cv::Rect2f box;
    QString label;
    float score{0.0F};
};

/// Maps coordinates in a letterboxed inference input back onto the source frame.
///
/// Letterboxing rather than a plain resize matters for pose: a stretched person
/// has limb proportions the model was never trained on, and the keypoints drift
/// in a way that looks like noise rather than like a bug.
struct LetterboxTransform
{
    float scale{1.0F};
    float padX{0.0F};
    float padY{0.0F};

    cv::Point2f toSource(float x, float y) const
    {
        return cv::Point2f{(x - padX) / scale, (y - padY) / scale};
    }
};

/// Resizes `source` into a square `side` by `side` image, preserving aspect
/// ratio and padding the remainder with the neutral grey the YOLO family was
/// trained against.
LetterboxTransform letterbox(const cv::Mat &source, cv::Mat &destination, int side);

} // namespace Kerfur

#endif // KERFUR_VISIONTYPES_H
