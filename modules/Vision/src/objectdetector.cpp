// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "objectdetector.h"

#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(objectLog, "kerfur.vision.objects")

constexpr int cocoClassCount{80};

// Four box sides, each a distribution over eight discrete distances. This is
// the generalized focal loss head: the network scores how far the edge is
// rather than regressing one number, and the decode takes the expectation.
constexpr int regressionBins{8};
constexpr int regressionChannels{4 * regressionBins};

// Per channel normalization the upstream demo applies, in BGR order because
// that is the order the frame already has.
constexpr float channelMean[]{103.53F, 116.28F, 123.675F};
constexpr float channelStd[]{57.375F, 57.12F, 58.395F};

} // namespace

const QStringList &cocoClassNames()
{
    static const QStringList names{
        QStringLiteral("person"),        QStringLiteral("bicycle"),
        QStringLiteral("car"),           QStringLiteral("motorcycle"),
        QStringLiteral("airplane"),      QStringLiteral("bus"),
        QStringLiteral("train"),         QStringLiteral("truck"),
        QStringLiteral("boat"),          QStringLiteral("traffic light"),
        QStringLiteral("fire hydrant"),  QStringLiteral("stop sign"),
        QStringLiteral("parking meter"), QStringLiteral("bench"),
        QStringLiteral("bird"),          QStringLiteral("cat"),
        QStringLiteral("dog"),           QStringLiteral("horse"),
        QStringLiteral("sheep"),         QStringLiteral("cow"),
        QStringLiteral("elephant"),      QStringLiteral("bear"),
        QStringLiteral("zebra"),         QStringLiteral("giraffe"),
        QStringLiteral("backpack"),      QStringLiteral("umbrella"),
        QStringLiteral("handbag"),       QStringLiteral("tie"),
        QStringLiteral("suitcase"),      QStringLiteral("frisbee"),
        QStringLiteral("skis"),          QStringLiteral("snowboard"),
        QStringLiteral("sports ball"),   QStringLiteral("kite"),
        QStringLiteral("baseball bat"),  QStringLiteral("baseball glove"),
        QStringLiteral("skateboard"),    QStringLiteral("surfboard"),
        QStringLiteral("tennis racket"), QStringLiteral("bottle"),
        QStringLiteral("wine glass"),    QStringLiteral("cup"),
        QStringLiteral("fork"),          QStringLiteral("knife"),
        QStringLiteral("spoon"),         QStringLiteral("bowl"),
        QStringLiteral("banana"),        QStringLiteral("apple"),
        QStringLiteral("sandwich"),      QStringLiteral("orange"),
        QStringLiteral("broccoli"),      QStringLiteral("carrot"),
        QStringLiteral("hot dog"),       QStringLiteral("pizza"),
        QStringLiteral("donut"),         QStringLiteral("cake"),
        QStringLiteral("chair"),         QStringLiteral("couch"),
        QStringLiteral("potted plant"),  QStringLiteral("bed"),
        QStringLiteral("dining table"),  QStringLiteral("toilet"),
        QStringLiteral("tv"),            QStringLiteral("laptop"),
        QStringLiteral("mouse"),         QStringLiteral("remote"),
        QStringLiteral("keyboard"),      QStringLiteral("cell phone"),
        QStringLiteral("microwave"),     QStringLiteral("oven"),
        QStringLiteral("toaster"),       QStringLiteral("sink"),
        QStringLiteral("refrigerator"),  QStringLiteral("book"),
        QStringLiteral("clock"),         QStringLiteral("vase"),
        QStringLiteral("scissors"),      QStringLiteral("teddy bear"),
        QStringLiteral("hair drier"),    QStringLiteral("toothbrush")};
    return names;
}

bool ObjectDetector::load(const QString &modelPath)
{
    m_loaded = false;
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());
    } catch (const cv::Exception &error) {
        qCWarning(objectLog) << "cannot read" << modelPath << error.what();
        return false;
    }
    if (m_net.empty()) {
        qCWarning(objectLog) << "empty network from" << modelPath;
        return false;
    }

    m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    m_loaded = true;
    return true;
}

QList<ObjectDetection> ObjectDetector::detect(const cv::Mat &bgr)
{
    if (!m_loaded || bgr.empty()) {
        return {};
    }

    try {
        // A plain resize rather than a letterbox, because that is what this
        // model was exported and calibrated against. Mixing the two conventions
        // is how boxes end up consistently offset by the padding band.
        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size{inputSide, inputSide}, 0.0, 0.0, cv::INTER_LINEAR);

        // Split rather than a Scalar expression: the per channel standard
        // deviations differ, and OpenCV's Mat by Scalar division is defined for
        // a uniform divisor only.
        cv::Mat normalized;
        resized.convertTo(normalized, CV_32FC3);
        std::vector<cv::Mat> planes;
        cv::split(normalized, planes);
        for (int plane{0}; plane < 3; ++plane) {
            planes[static_cast<size_t>(plane)] =
                (planes[static_cast<size_t>(plane)] - channelMean[plane]) / channelStd[plane];
        }
        cv::merge(planes, normalized);

        const cv::Mat blob{cv::dnn::blobFromImage(normalized)};
        m_net.setInput(blob);

        std::vector<cv::Mat> outputs;
        m_net.forward(outputs, m_net.getUnconnectedOutLayersNames());

        return decodeNanoDet(outputs, bgr.size(), m_confidenceThreshold, m_nmsThreshold);
    } catch (const cv::Exception &error) {
        qCWarning(objectLog) << "inference failed" << error.what();
        return {};
    }
}

QList<ObjectDetection> ObjectDetector::detect(const cv::Mat &bgr, const cv::Rect &region)
{
    if (!m_loaded || bgr.empty()) {
        return {};
    }
    const cv::Rect roi{region & cv::Rect{0, 0, bgr.cols, bgr.rows}};
    if (roi.width < 2 || roi.height < 2) {
        return {};
    }

    QList<ObjectDetection> found{detect(bgr(roi))};
    for (ObjectDetection &detection : found) {
        detection.box.x += static_cast<float>(roi.x);
        detection.box.y += static_cast<float>(roi.y);
    }
    return found;
}

QList<ObjectDetection> decodeNanoDet(const std::vector<cv::Mat> &outputs,
                                     const cv::Size &sourceSize,
                                     float confidenceThreshold,
                                     float nmsThreshold)
{
    if (outputs.empty() || sourceSize.width <= 0 || sourceSize.height <= 0) {
        return {};
    }

    const float scaleX{static_cast<float>(sourceSize.width)
                       / static_cast<float>(ObjectDetector::inputSide)};
    const float scaleY{static_cast<float>(sourceSize.height)
                       / static_cast<float>(ObjectDetector::inputSide)};

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classes;

    for (const cv::Mat &classTensor : outputs) {
        if (classTensor.dims != 3 || classTensor.size[2] != cocoClassCount
            || classTensor.type() != CV_32F) {
            continue;
        }
        const int anchors{classTensor.size[1]};

        // The partner regression tensor is the one with the same anchor count.
        const cv::Mat *regressionTensor{nullptr};
        for (const cv::Mat &candidate : outputs) {
            if (candidate.dims == 3 && candidate.size[1] == anchors
                && candidate.size[2] == regressionChannels && candidate.type() == CV_32F) {
                regressionTensor = &candidate;
                break;
            }
        }
        if (regressionTensor == nullptr) {
            continue;
        }

        // Anchors are one per cell of a square feature map, so the grid width
        // and therefore the stride follow from the count alone. Nothing in the
        // graph states the stride, and hard coding a list would silently break
        // against a re-export with a different pyramid.
        const int gridWidth{static_cast<int>(std::lround(std::sqrt(anchors)))};
        if (gridWidth <= 0 || (gridWidth * gridWidth) != anchors) {
            continue;
        }
        const float stride{static_cast<float>(ObjectDetector::inputSide)
                           / static_cast<float>(gridWidth)};

        const float *const classData{classTensor.ptr<float>()};
        const float *const regressionData{regressionTensor->ptr<float>()};

        for (int anchor{0}; anchor < anchors; ++anchor) {
            const float *const anchorScores{classData + (static_cast<qsizetype>(anchor)
                                                         * cocoClassCount)};
            const int bestClass{static_cast<int>(
                std::distance(anchorScores,
                              std::max_element(anchorScores, anchorScores + cocoClassCount)))};
            const float bestScore{anchorScores[bestClass]};
            if (bestScore < confidenceThreshold) {
                continue;
            }

            const float centreX{(static_cast<float>(anchor % gridWidth) + 0.5F) * stride};
            const float centreY{(static_cast<float>(anchor / gridWidth) + 0.5F) * stride};

            const float *const anchorRegression{
                regressionData + (static_cast<qsizetype>(anchor) * regressionChannels)};
            float distances[4]{};
            for (int side{0}; side < 4; ++side) {
                const float *const bins{anchorRegression + (side * regressionBins)};
                const float largest{*std::max_element(bins, bins + regressionBins)};

                // Softmax with the maximum subtracted. These are logits and can
                // reach the high single digits, which is enough for a naive exp
                // to lose precision well before it overflows.
                float total{0.0F};
                float expectation{0.0F};
                for (int bin{0}; bin < regressionBins; ++bin) {
                    const float weight{std::exp(bins[bin] - largest)};
                    total += weight;
                    expectation += weight * static_cast<float>(bin);
                }
                distances[side] = total > 0.0F ? (expectation / total) * stride : 0.0F;
            }

            const float left{(centreX - distances[0]) * scaleX};
            const float top{(centreY - distances[1]) * scaleY};
            const float right{(centreX + distances[2]) * scaleX};
            const float bottom{(centreY + distances[3]) * scaleY};

            boxes.emplace_back(cv::Rect{static_cast<int>(std::lround(left)),
                                        static_cast<int>(std::lround(top)),
                                        static_cast<int>(std::lround(right - left)),
                                        static_cast<int>(std::lround(bottom - top))});
            scores.push_back(bestScore);
            classes.push_back(bestClass);
        }
    }

    std::vector<int> kept;
    cv::dnn::NMSBoxes(boxes, scores, confidenceThreshold, nmsThreshold, kept);

    const QStringList &names{cocoClassNames()};
    const cv::Rect2f frame{0.0F,
                           0.0F,
                           static_cast<float>(sourceSize.width),
                           static_cast<float>(sourceSize.height)};

    QList<ObjectDetection> detections;
    detections.reserve(static_cast<qsizetype>(kept.size()));
    for (const int index : kept) {
        const cv::Rect &box{boxes.at(static_cast<size_t>(index))};

        ObjectDetection detection;
        detection.box = cv::Rect2f{static_cast<float>(box.x),
                                   static_cast<float>(box.y),
                                   static_cast<float>(box.width),
                                   static_cast<float>(box.height)}
                        & frame;
        detection.score = scores.at(static_cast<size_t>(index));
        detection.label = names.value(classes.at(static_cast<size_t>(index)));
        detections.append(detection);
    }
    return detections;
}

} // namespace Kerfur
