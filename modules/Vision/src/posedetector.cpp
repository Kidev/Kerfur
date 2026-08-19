// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "posedetector.h"

#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <vector>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(poseLog, "kerfur.vision.pose")

// 4 box values, 1 person confidence, then 17 keypoints of (x, y, score). The
// export has a single class, so there is no class score to search.
constexpr int poseChannels{5 + (3 * keypointCount)};

} // namespace

bool PoseDetector::load(const QString &modelPath)
{
    m_loaded = false;
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());
    } catch (const cv::Exception &error) {
        qCWarning(poseLog) << "cannot read" << modelPath << error.what();
        return false;
    }
    if (m_net.empty()) {
        qCWarning(poseLog) << "empty network from" << modelPath;
        return false;
    }

    m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    m_loaded = true;
    return true;
}

QList<PersonDetection> PoseDetector::detect(const cv::Mat &bgr)
{
    if (!m_loaded || bgr.empty()) {
        return {};
    }

    try {
        cv::Mat letterboxed;
        const LetterboxTransform transform{letterbox(bgr, letterboxed, inputSide)};

        // swapRB because the model was trained on RGB and the capture path
        // hands over BGR, which is the one preprocessing mistake that produces
        // detections just good enough to look like it worked.
        const cv::Mat blob{cv::dnn::blobFromImage(letterboxed,
                                                  1.0 / 255.0,
                                                  cv::Size{inputSide, inputSide},
                                                  cv::Scalar{},
                                                  true,
                                                  false)};
        m_net.setInput(blob);
        const cv::Mat output{m_net.forward()};

        QList<PersonDetection> detections{
            decodeYoloPose(output, transform, m_confidenceThreshold, m_nmsThreshold)};

        const cv::Rect2f frame{0.0F,
                               0.0F,
                               static_cast<float>(bgr.cols),
                               static_cast<float>(bgr.rows)};
        for (PersonDetection &detection : detections) {
            detection.box = detection.box & frame;
        }
        return detections;
    } catch (const cv::Exception &error) {
        qCWarning(poseLog) << "inference failed" << error.what();
        return {};
    }
}

QList<PersonDetection> decodeYoloPose(const cv::Mat &output,
                                      const LetterboxTransform &transform,
                                      float confidenceThreshold,
                                      float nmsThreshold)
{
    if (output.dims != 3 || output.size[0] != 1 || output.size[1] != poseChannels
        || output.type() != CV_32F) {
        return {};
    }

    const int anchors{output.size[2]};
    if (anchors <= 0) {
        return {};
    }
    const float *const data{output.ptr<float>()};

    // Channel major: every channel is a contiguous run of `anchors` values, so
    // one anchor's 56 numbers are strided rather than adjacent. Transposing the
    // whole tensor first would be the obvious thing and is pure waste, because
    // most anchors fail the confidence test on their first read.
    const auto channel{[data, anchors](int index, int anchor) -> float {
        return data[(static_cast<qsizetype>(index) * anchors) + anchor];
    }};

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> keptAnchors;
    boxes.reserve(64);
    scores.reserve(64);
    keptAnchors.reserve(64);

    for (int anchor{0}; anchor < anchors; ++anchor) {
        const float score{channel(4, anchor)};
        if (score < confidenceThreshold) {
            continue;
        }

        const float centreX{channel(0, anchor)};
        const float centreY{channel(1, anchor)};
        const float width{channel(2, anchor)};
        const float height{channel(3, anchor)};

        // Non maximum suppression happens in letterboxed space. Doing it after
        // the mapping back would give the same answer for this transform, but
        // only because it is a uniform scale; keeping it here means the box
        // list stays in one coordinate system.
        boxes.emplace_back(cv::Rect{static_cast<int>(std::lround(centreX - (width / 2.0F))),
                                    static_cast<int>(std::lround(centreY - (height / 2.0F))),
                                    static_cast<int>(std::lround(width)),
                                    static_cast<int>(std::lround(height))});
        scores.push_back(score);
        keptAnchors.push_back(anchor);
    }

    std::vector<int> kept;
    cv::dnn::NMSBoxes(boxes, scores, confidenceThreshold, nmsThreshold, kept);

    QList<PersonDetection> detections;
    detections.reserve(static_cast<qsizetype>(kept.size()));
    for (const int index : kept) {
        const int anchor{keptAnchors.at(static_cast<size_t>(index))};

        const float centreX{channel(0, anchor)};
        const float centreY{channel(1, anchor)};
        const float width{channel(2, anchor)};
        const float height{channel(3, anchor)};

        const cv::Point2f topLeft{
            transform.toSource(centreX - (width / 2.0F), centreY - (height / 2.0F))};
        const cv::Point2f bottomRight{
            transform.toSource(centreX + (width / 2.0F), centreY + (height / 2.0F))};

        PersonDetection detection;
        detection.score = scores.at(static_cast<size_t>(index));
        detection.box = cv::Rect2f{topLeft.x,
                                   topLeft.y,
                                   bottomRight.x - topLeft.x,
                                   bottomRight.y - topLeft.y};

        for (int keypoint{0}; keypoint < keypointCount; ++keypoint) {
            const int base{5 + (keypoint * 3)};
            const cv::Point2f position{
                transform.toSource(channel(base, anchor), channel(base + 1, anchor))};
            detection.keypoints[static_cast<size_t>(keypoint)] = Keypoint2D{
                QPointF{position.x, position.y}, channel(base + 2, anchor)};
        }
        detections.append(detection);
    }
    return detections;
}

} // namespace Kerfur
