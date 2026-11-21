// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ACCELERATORDECODE_H
#define KERFUR_ACCELERATORDECODE_H

#include "accelerator.h"
#include "visiontypes.h"

#include <QList>

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Kerfur {

/// One face SCRFD found, in source frame pixels. The landmarks are in the
/// order the alignment template uses: the eye on the left of the picture, the
/// eye on the right, the nose tip, the mouth corner on the left, the one on the
/// right.
struct FaceDetection
{
    cv::Rect2f box;
    std::array<cv::Point2f, 5> landmarks{};
    float score{0.0F};
};

/// Decodes the raw heads of a compiled YOLOv8 pose model.
///
/// Three strides, each a triple of tensors told apart by channel count: 64 for
/// the box (four sides of sixteen distribution bins), 1 for the person score
/// (already through a sigmoid on the device), 51 for the keypoints (x, y and a
/// visibility logit for each of seventeen). The stride of a triple is the
/// model's input side over its grid width. `inputSide` is that side and
/// `transform` maps the letterboxed input back to the source frame.
QList<PersonDetection> decodeYoloPoseHeads(const std::vector<Accel::Tensor> &outputs,
                                           int inputSide,
                                           const LetterboxTransform &transform,
                                           float confidenceThreshold,
                                           float nmsThreshold);

/// Decodes SCRFD's three strides, each a triple told apart by channel count:
/// 8 for the boxes of the two anchors per cell (left, top, right and bottom
/// distances in units of the stride), 2 for their scores (already through a
/// sigmoid on the device), 20 for their five landmarks.
QList<FaceDetection> decodeScrfd(const std::vector<Accel::Tensor> &outputs,
                                 int inputSide,
                                 const LetterboxTransform &transform,
                                 float confidenceThreshold,
                                 float nmsThreshold);

/// Runs `model` on `rgb`, which must already have the model's input shape and
/// be 8 bit RGB. Returns an empty string on success, otherwise why not.
std::string runImage(Accel::Model &model, const cv::Mat &rgb, std::vector<Accel::Tensor> &outputs);

/// Letterboxes `bgr` into the model's square input as RGB and runs it.
/// `transform` maps the input back onto `bgr`.
std::string runLetterboxed(Accel::Model &model,
                           const cv::Mat &bgr,
                           LetterboxTransform &transform,
                           std::vector<Accel::Tensor> &outputs);

/// The output whose name ends in `/suffix`, or null. A compiled model names
/// its outputs `network/layer`, and the layer is what a decoder knows.
const Accel::Tensor *outputNamed(const std::vector<Accel::Tensor> &outputs,
                                 std::string_view suffix);

/// The element of a tensor at one grid cell and channel, NHWC.
inline float tensorAt(const Accel::Tensor &tensor, int row, int column, int channel)
{
    const auto index{(static_cast<std::size_t>(row) * static_cast<std::size_t>(tensor.info.width) +
                      static_cast<std::size_t>(column)) *
                         static_cast<std::size_t>(tensor.info.channels) +
                     static_cast<std::size_t>(channel)};
    return tensor.values[index];
}

} // namespace Kerfur

#endif // KERFUR_ACCELERATORDECODE_H
