// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "acceleratordecode.h"

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace Kerfur {

namespace {

/// The bins of one side of a YOLOv8 box distribution.
constexpr int distributionBins{16};
constexpr int poseBoxChannels{4 * distributionBins};
constexpr int poseKeypointChannels{3 * keypointCount};

constexpr int scrfdAnchors{2};
constexpr int scrfdLandmarks{5};

/// The tensors of one stride, picked out of an output list by channel count.
struct Level
{
    const Accel::Tensor *box{nullptr};
    const Accel::Tensor *score{nullptr};
    const Accel::Tensor *extra{nullptr};
};

/// Groups grid outputs by their grid size, and within a grid by the three
/// channel counts that identify box, score and extra. Names are not used: they
/// are layer numbers that change with every compilation of the same network.
std::map<std::pair<int, int>, Level> levelsOf(const std::vector<Accel::Tensor> &outputs,
                                              int boxChannels,
                                              int scoreChannels,
                                              int extraChannels)
{
    std::map<std::pair<int, int>, Level> levels;
    for (const Accel::Tensor &tensor : outputs) {
        if (tensor.info.nms || tensor.values.size() != tensor.info.elements()) {
            continue;
        }
        Level &level{levels[{tensor.info.height, tensor.info.width}]};
        if (tensor.info.channels == boxChannels) {
            level.box = &tensor;
        } else if (tensor.info.channels == scoreChannels) {
            level.score = &tensor;
        } else if (tensor.info.channels == extraChannels) {
            level.extra = &tensor;
        }
    }
    std::erase_if(levels, [](const auto &entry) {
        return entry.second.box == nullptr || entry.second.score == nullptr ||
               entry.second.extra == nullptr;
    });
    return levels;
}

float sigmoid(float value)
{
    return 1.0F / (1.0F + std::exp(-value));
}

/// The expected bin of one side's distribution, which is the distance from the
/// cell centre to that side in units of the stride.
float expectedDistance(const Accel::Tensor &box, int row, int column, int side)
{
    const int first{side * distributionBins};
    float largest{tensorAt(box, row, column, first)};
    for (int bin{1}; bin < distributionBins; ++bin) {
        largest = std::max(largest, tensorAt(box, row, column, first + bin));
    }
    float total{0.0F};
    float weighted{0.0F};
    for (int bin{0}; bin < distributionBins; ++bin) {
        const float weight{std::exp(tensorAt(box, row, column, first + bin) - largest)};
        total += weight;
        weighted += weight * static_cast<float>(bin);
    }
    return total > 0.0F ? weighted / total : 0.0F;
}

cv::Rect2f mapBox(
    const LetterboxTransform &transform, float left, float top, float right, float bottom)
{
    const cv::Point2f topLeft{transform.toSource(left, top)};
    const cv::Point2f bottomRight{transform.toSource(right, bottom)};
    return cv::Rect2f{topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
}

std::vector<int> suppress(const std::vector<cv::Rect2d> &boxes,
                          const std::vector<float> &scores,
                          float confidenceThreshold,
                          float nmsThreshold)
{
    std::vector<int> kept;
    if (!boxes.empty()) {
        cv::dnn::NMSBoxes(boxes, scores, confidenceThreshold, nmsThreshold, kept);
    }
    return kept;
}

} // namespace

std::string runImage(Accel::Model &model, const cv::Mat &rgb, std::vector<Accel::Tensor> &outputs)
{
    const Accel::TensorInfo &input{model.input()};
    if (rgb.type() != CV_8UC3 || rgb.rows != input.height || rgb.cols != input.width) {
        return "image is " + std::to_string(rgb.cols) + "x" + std::to_string(rgb.rows) +
               ", the model takes " + std::to_string(input.width) + "x" +
               std::to_string(input.height) + " RGB";
    }
    const cv::Mat contiguous{rgb.isContinuous() ? rgb : rgb.clone()};
    return model.run(contiguous.ptr<std::uint8_t>(), contiguous.total() * 3, outputs);
}

std::string runLetterboxed(Accel::Model &model,
                           const cv::Mat &bgr,
                           LetterboxTransform &transform,
                           std::vector<Accel::Tensor> &outputs)
{
    const Accel::TensorInfo &input{model.input()};
    if (input.height != input.width) {
        return "letterboxing needs a square input, the model takes " + std::to_string(input.width) +
               "x" + std::to_string(input.height);
    }
    cv::Mat letterboxed;
    transform = letterbox(bgr, letterboxed, input.width);
    cv::Mat rgb;
    cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
    return runImage(model, rgb, outputs);
}

const Accel::Tensor *outputNamed(const std::vector<Accel::Tensor> &outputs, std::string_view suffix)
{
    for (const Accel::Tensor &tensor : outputs) {
        const std::string_view name{tensor.info.name};
        if (name.size() > suffix.size() && name.ends_with(suffix) &&
            name[name.size() - suffix.size() - 1] == '/') {
            return &tensor;
        }
    }
    return nullptr;
}

QList<PersonDetection> decodeYoloPoseHeads(const std::vector<Accel::Tensor> &outputs,
                                           int inputSide,
                                           const LetterboxTransform &transform,
                                           float confidenceThreshold,
                                           float nmsThreshold)
{
    struct Candidate
    {
        cv::Rect2d box;
        std::array<Keypoint2D, keypointCount> keypoints{};
    };
    std::vector<Candidate> candidates;
    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;

    for (const auto &[grid, level] : levelsOf(outputs, poseBoxChannels, 1, poseKeypointChannels)) {
        const int rows{grid.first};
        const int columns{grid.second};
        if (columns <= 0 || rows <= 0) {
            continue;
        }
        const float stride{static_cast<float>(inputSide) / static_cast<float>(columns)};
        for (int row{0}; row < rows; ++row) {
            for (int column{0}; column < columns; ++column) {
                const float score{tensorAt(*level.score, row, column, 0)};
                if (score < confidenceThreshold) {
                    continue;
                }
                const float centreX{(static_cast<float>(column) + 0.5F) * stride};
                const float centreY{(static_cast<float>(row) + 0.5F) * stride};
                const float left{centreX - expectedDistance(*level.box, row, column, 0) * stride};
                const float top{centreY - expectedDistance(*level.box, row, column, 1) * stride};
                const float right{centreX + expectedDistance(*level.box, row, column, 2) * stride};
                const float bottom{centreY + expectedDistance(*level.box, row, column, 3) * stride};

                Candidate candidate;
                candidate.box = cv::Rect2d{left, top, right - left, bottom - top};
                for (int keypoint{0}; keypoint < keypointCount; ++keypoint) {
                    const int base{keypoint * 3};
                    const float x{(tensorAt(*level.extra, row, column, base) * 2.0F +
                                   static_cast<float>(column)) *
                                  stride};
                    const float y{(tensorAt(*level.extra, row, column, base + 1) * 2.0F +
                                   static_cast<float>(row)) *
                                  stride};
                    const cv::Point2f source{transform.toSource(x, y)};
                    candidate.keypoints[static_cast<std::size_t>(keypoint)] =
                        Keypoint2D{QPointF{source.x, source.y},
                                   sigmoid(tensorAt(*level.extra, row, column, base + 2))};
                }
                boxes.push_back(candidate.box);
                scores.push_back(score);
                candidates.push_back(candidate);
            }
        }
    }

    QList<PersonDetection> detections;
    for (const int index : suppress(boxes, scores, confidenceThreshold, nmsThreshold)) {
        const Candidate &candidate{candidates.at(static_cast<std::size_t>(index))};
        PersonDetection detection;
        detection.score = scores.at(static_cast<std::size_t>(index));
        detection.box = mapBox(transform,
                               static_cast<float>(candidate.box.x),
                               static_cast<float>(candidate.box.y),
                               static_cast<float>(candidate.box.x + candidate.box.width),
                               static_cast<float>(candidate.box.y + candidate.box.height));
        detection.keypoints = candidate.keypoints;
        detections.append(detection);
    }
    return detections;
}

QList<FaceDetection> decodeScrfd(const std::vector<Accel::Tensor> &outputs,
                                 int inputSide,
                                 const LetterboxTransform &transform,
                                 float confidenceThreshold,
                                 float nmsThreshold)
{
    struct Candidate
    {
        cv::Rect2d box;
        std::array<cv::Point2f, scrfdLandmarks> landmarks{};
    };
    std::vector<Candidate> candidates;
    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;

    for (const auto &[grid, level] :
         levelsOf(outputs, 4 * scrfdAnchors, scrfdAnchors, 2 * scrfdLandmarks * scrfdAnchors)) {
        const int rows{grid.first};
        const int columns{grid.second};
        if (columns <= 0 || rows <= 0) {
            continue;
        }
        const float stride{static_cast<float>(inputSide) / static_cast<float>(columns)};
        for (int row{0}; row < rows; ++row) {
            for (int column{0}; column < columns; ++column) {
                // SCRFD's anchor sits on the cell corner, not its centre.
                const float centreX{static_cast<float>(column) * stride};
                const float centreY{static_cast<float>(row) * stride};
                for (int anchor{0}; anchor < scrfdAnchors; ++anchor) {
                    const float score{tensorAt(*level.score, row, column, anchor)};
                    if (score < confidenceThreshold) {
                        continue;
                    }
                    const int boxBase{anchor * 4};
                    const float left{centreX - tensorAt(*level.box, row, column, boxBase) * stride};
                    const float top{centreY -
                                    tensorAt(*level.box, row, column, boxBase + 1) * stride};
                    const float right{centreX +
                                      tensorAt(*level.box, row, column, boxBase + 2) * stride};
                    const float bottom{centreY +
                                       tensorAt(*level.box, row, column, boxBase + 3) * stride};

                    Candidate candidate;
                    candidate.box = cv::Rect2d{left, top, right - left, bottom - top};
                    const int landmarkBase{anchor * 2 * scrfdLandmarks};
                    for (int landmark{0}; landmark < scrfdLandmarks; ++landmark) {
                        const int channel{landmarkBase + landmark * 2};
                        const float x{centreX
                                      + tensorAt(*level.extra, row, column, channel) * stride};
                        const float y{centreY
                                      + tensorAt(*level.extra, row, column, channel + 1) * stride};
                        candidate.landmarks[static_cast<std::size_t>(landmark)]
                            = transform.toSource(x, y);
                    }
                    boxes.push_back(candidate.box);
                    scores.push_back(score);
                    candidates.push_back(candidate);
                }
            }
        }
    }

    QList<FaceDetection> faces;
    for (const int index : suppress(boxes, scores, confidenceThreshold, nmsThreshold)) {
        const Candidate &candidate{candidates.at(static_cast<std::size_t>(index))};
        FaceDetection face;
        face.score = scores.at(static_cast<std::size_t>(index));
        face.box = mapBox(transform,
                          static_cast<float>(candidate.box.x),
                          static_cast<float>(candidate.box.y),
                          static_cast<float>(candidate.box.x + candidate.box.width),
                          static_cast<float>(candidate.box.y + candidate.box.height));
        face.landmarks = candidate.landmarks;
        faces.append(face);
    }
    return faces;
}

} // namespace Kerfur
