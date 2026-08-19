// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "visiontypes.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace Kerfur {

namespace {

// The fill Ultralytics letterboxes with. Any constant would decode correctly,
// but a value the model saw during training keeps the padded band from reading
// as an edge feature.
constexpr double letterboxFill{114.0};

} // namespace

LetterboxTransform letterbox(const cv::Mat &source, cv::Mat &destination, int side)
{
    LetterboxTransform transform;
    if (source.empty() || side <= 0) {
        destination = cv::Mat::zeros(std::max(side, 1), std::max(side, 1), CV_8UC3);
        return transform;
    }

    const float scale{
        std::min(static_cast<float>(side) / static_cast<float>(source.cols),
                 static_cast<float>(side) / static_cast<float>(source.rows))};
    const int scaledWidth{std::max(1, static_cast<int>(std::lround(source.cols * scale)))};
    const int scaledHeight{std::max(1, static_cast<int>(std::lround(source.rows * scale)))};

    cv::Mat resized;
    // INTER_AREA when shrinking, which is the usual case: it averages the
    // discarded pixels instead of sampling one of them, and a sampled downscale
    // makes thin limbs disappear at range.
    const int interpolation{scale < 1.0F ? cv::INTER_AREA : cv::INTER_LINEAR};
    cv::resize(source, resized, cv::Size{scaledWidth, scaledHeight}, 0.0, 0.0, interpolation);

    const int padX{(side - scaledWidth) / 2};
    const int padY{(side - scaledHeight) / 2};
    destination.create(side, side, source.type());
    destination.setTo(cv::Scalar::all(letterboxFill));
    resized.copyTo(destination(cv::Rect{padX, padY, scaledWidth, scaledHeight}));

    transform.scale = scale;
    transform.padX = static_cast<float>(padX);
    transform.padY = static_cast<float>(padY);
    return transform;
}

} // namespace Kerfur
