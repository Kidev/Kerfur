// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_OBJECTDETECTOR_H
#define KERFUR_OBJECTDETECTOR_H

#include "visiontypes.h"

#include <QList>
#include <QString>

#include <opencv2/dnn.hpp>

#include <vector>

namespace Kerfur {

/// Stage B. COCO's eighty classes, which is what gives a held object an exact
/// label rather than a semantic guess.
///
/// On the Pi this stage is merged with CLIP, which fills in what COCO has no
/// class for. Here it is the whole answer, and section 5.5 of the design
/// already makes COCO authoritative wherever it has a class, so the merge rule
/// survives with one input instead of two.
class ObjectDetector
{
public:
    bool load(const QString &modelPath);
    bool isLoaded() const { return m_loaded; }

    QList<ObjectDetection> detect(const cv::Mat &bgr);

    /// The same, over a crop of `bgr`, with the boxes given back in the
    /// coordinates of the whole frame.
    ///
    /// The network has one fixed input side, so what a crop buys is resolution
    /// where it matters: an object held in a hand is a handful of pixels once a
    /// whole room has been squeezed into 416 by 416, and a handful of pixels is
    /// enough to detect something and nowhere near enough to classify it.
    QList<ObjectDetection> detect(const cv::Mat &bgr, const cv::Rect &region);

    void setConfidenceThreshold(float threshold) { m_confidenceThreshold = threshold; }
    void setNmsThreshold(float threshold) { m_nmsThreshold = threshold; }

    static constexpr int inputSide{416};

private:
    cv::dnn::Net m_net;
    bool m_loaded{false};
    float m_confidenceThreshold{0.35F};
    float m_nmsThreshold{0.6F};
};

/// Decodes NanoDet-Plus' per stride output pairs into source frame boxes.
///
/// The outputs arrive as one class tensor and one regression tensor per stride,
/// and they are matched up by anchor count rather than by output name, because
/// the name order is an artefact of the ONNX export and nothing promises to
/// keep it.
QList<ObjectDetection> decodeNanoDet(const std::vector<cv::Mat> &outputs,
                                     const cv::Size &sourceSize,
                                     float confidenceThreshold,
                                     float nmsThreshold);

/// The eighty COCO class names, in the order the model scores them.
const QStringList &cocoClassNames();

} // namespace Kerfur

#endif // KERFUR_OBJECTDETECTOR_H
