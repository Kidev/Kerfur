// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "motiongate.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace Kerfur {

namespace {

cv::Size thumbnailSize(int width, int height)
{
    const int thumbHeight{
        std::max(1,
                 static_cast<int>(std::lround(static_cast<double>(MotionGate::thumbnailWidth) *
                                              height / width)))};
    return cv::Size{MotionGate::thumbnailWidth, thumbHeight};
}

} // namespace

QImage MotionGate::thumbnail(const uchar *plane, int width, int height, qsizetype stride)
{
    if (plane == nullptr || width <= 0 || height <= 0 || stride < width) {
        return {};
    }
    const cv::Size size{thumbnailSize(width, height)};
    QImage reduced{size.width, size.height, QImage::Format_Grayscale8};
    try {
        // OpenCV only reads through this header; the constructor is not const
        // correct.
        const cv::Mat source{height,
                             width,
                             CV_8UC1,
                             const_cast<uchar *>(plane),
                             static_cast<size_t>(stride)};
        cv::Mat target{size.height,
                       size.width,
                       CV_8UC1,
                       reduced.bits(),
                       static_cast<size_t>(reduced.bytesPerLine())};
        cv::resize(source, target, size, 0.0, 0.0, cv::INTER_AREA);
        // Written in place only while the destination already had the right
        // size and type, which it does unless OpenCV had to reallocate.
        if (target.data != reduced.bits()) {
            return {};
        }
    } catch (const cv::Exception &) {
        return {};
    }
    return reduced;
}

QImage MotionGate::thumbnail(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }
    if (image.format() == QImage::Format_Grayscale8) {
        return thumbnail(image.constBits(), image.width(), image.height(), image.bytesPerLine());
    }

    QImage readable{image};
    int type{CV_8UC3};
    int toGrey{cv::COLOR_RGB2GRAY};
    switch (image.format()) {
    case QImage::Format_RGB888:
        break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
        // 0xAARRGGBB words, which a little endian machine stores as B, G, R, A.
        type = CV_8UC4;
        toGrey = cv::COLOR_BGRA2GRAY;
        break;
    default:
        readable = image.convertToFormat(QImage::Format_RGB888);
        break;
    }

    const cv::Size size{thumbnailSize(readable.width(), readable.height())};
    QImage grey{size.width, size.height, QImage::Format_Grayscale8};
    try {
        const cv::Mat source{readable.height(),
                             readable.width(),
                             type,
                             const_cast<uchar *>(readable.constBits()),
                             static_cast<size_t>(readable.bytesPerLine())};
        cv::Mat reduced;
        cv::resize(source, reduced, size, 0.0, 0.0, cv::INTER_AREA);
        cv::Mat target{size.height,
                       size.width,
                       CV_8UC1,
                       grey.bits(),
                       static_cast<size_t>(grey.bytesPerLine())};
        cv::cvtColor(reduced, target, toGrey);
        if (target.data != grey.bits()) {
            return {};
        }
    } catch (const cv::Exception &) {
        return {};
    }
    return grey;
}

double MotionGate::changedFraction(const QImage &thumbnail) const
{
    if (m_reference.isNull() || thumbnail.isNull() || thumbnail.size() != m_reference.size() ||
        thumbnail.format() != QImage::Format_Grayscale8 ||
        m_reference.format() != QImage::Format_Grayscale8) {
        return 1.0;
    }

    const int rows{thumbnail.height()};
    const int cols{thumbnail.width()};
    const long long count{static_cast<long long>(rows) * cols};

    // The exposure shift is the median difference rather than the mean: a
    // quarter of the picture turning bright moves the mean by a quarter of that
    // and would make the other three quarters look changed, while the median
    // stays with the part of the picture that did not move.
    std::array<long long, 511> histogram{};
    for (int row{0}; row < rows; ++row) {
        const uchar *now{thumbnail.constScanLine(row)};
        const uchar *then{m_reference.constScanLine(row)};
        for (int col{0}; col < cols; ++col) {
            ++histogram.at(static_cast<size_t>(static_cast<int>(now[col]) -
                                               static_cast<int>(then[col]) + 255));
        }
    }
    int shift{0};
    long long seen{0};
    for (size_t bin{0}; bin < histogram.size(); ++bin) {
        seen += histogram.at(bin);
        if (seen * 2 >= count) {
            shift = static_cast<int>(bin) - 255;
            break;
        }
    }

    long long changed{0};
    for (int row{0}; row < rows; ++row) {
        const uchar *now{thumbnail.constScanLine(row)};
        const uchar *then{m_reference.constScanLine(row)};
        for (int col{0}; col < cols; ++col) {
            const int difference{static_cast<int>(now[col]) - static_cast<int>(then[col]) - shift};
            if (std::abs(difference) >= pixelThreshold) {
                ++changed;
            }
        }
    }
    return static_cast<double>(changed) / static_cast<double>(count);
}

void IdleClock::setLimits(qint64 idleAfterMs, qint64 probeMs)
{
    m_idleAfterMs = std::max<qint64>(0, idleAfterMs);
    m_probeMs = std::max<qint64>(0, probeMs);
}

void IdleClock::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_watching = false;
    }
}

void IdleClock::reset(qint64 now)
{
    m_watching = false;
    m_probing = false;
    m_since = now;
}

void IdleClock::wake(qint64 now)
{
    m_watching = false;
    m_probing = true;
    m_since = now;
}

bool IdleClock::notePass(bool present, qint64 now)
{
    if (m_watching) {
        return false;
    }
    if (present) {
        m_probing = false;
        m_since = now;
        return false;
    }
    if (!m_enabled) {
        return false;
    }
    if (now - m_since >= (m_probing ? m_probeMs : m_idleAfterMs)) {
        m_watching = true;
        return true;
    }
    return false;
}

} // namespace Kerfur
