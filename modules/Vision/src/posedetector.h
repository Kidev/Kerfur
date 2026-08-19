// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_POSEDETECTOR_H
#define KERFUR_POSEDETECTOR_H

#include "visiontypes.h"

#include <QList>
#include <QString>

#include <opencv2/dnn.hpp>

namespace Kerfur {

/// Stage A. One pass over the whole frame producing person boxes and the
/// seventeen COCO keypoints, which is the same shape the accelerator's
/// `yolov8s_pose` produces on the Pi.
///
/// That parity is the point of this model choice: the tracker, the proximity
/// metric, the head centre and the upside down geometry check downstream are
/// one implementation shared by every platform, rather than two that have to be
/// kept in agreement.
class PoseDetector
{
public:
    bool load(const QString &modelPath);
    bool isLoaded() const { return m_loaded; }

    /// Returns an empty list rather than throwing when the model is not loaded
    /// or the frame is unusable. OpenCV throws and Kerfur does not, so its
    /// exceptions are converted here, at the boundary, and nowhere else.
    QList<PersonDetection> detect(const cv::Mat &bgr);

    void setConfidenceThreshold(float threshold) { m_confidenceThreshold = threshold; }
    void setNmsThreshold(float threshold) { m_nmsThreshold = threshold; }

    /// The square side the model was exported with. Fixed by the file, not a
    /// tuning knob: an ONNX graph with static input dimensions cannot be fed
    /// anything else.
    static constexpr int inputSide{640};

private:
    cv::dnn::Net m_net;
    bool m_loaded{false};
    float m_confidenceThreshold{0.35F};
    float m_nmsThreshold{0.45F};
};

/// Decodes the raw `1 x 56 x anchors` tensor into source frame coordinates.
///
/// Free standing so that the decode can be tested against a hand built tensor
/// without a model file or a camera, which is the only part of this backend
/// whose correctness is pure arithmetic.
QList<PersonDetection> decodeYoloPose(const cv::Mat &output,
                                      const LetterboxTransform &transform,
                                      float confidenceThreshold,
                                      float nmsThreshold);

} // namespace Kerfur

#endif // KERFUR_POSEDETECTOR_H
