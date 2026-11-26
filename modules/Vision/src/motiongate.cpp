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

/// One bin per difference between two grey levels, from -255 to 255; bin
/// `index` counts the pixels that changed by `index - 255`.
using DifferenceHistogram = std::array<long long, 511>;

// Turns the median absolute deviation of normally distributed noise into its
// standard deviation.
constexpr double deviationsPerMedianDeviation{1.4826};

cv::Size thumbnailSize(int width, int height)
{
    const int thumbHeight{
        std::max(1,
                 static_cast<int>(std::lround(static_cast<double>(MotionGate::thumbnailWidth) *
                                              height / width)))};
    return cv::Size{MotionGate::thumbnailWidth, thumbHeight};
}

bool isGrey(const QImage &image)
{
    return !image.isNull() && image.format() == QImage::Format_Grayscale8;
}

bool comparable(const QImage &thumbnail, const QImage &reference)
{
    return isGrey(thumbnail) && isGrey(reference) && thumbnail.size() == reference.size();
}

long long pixelCount(const QImage &image)
{
    return static_cast<long long>(image.width()) * image.height();
}

DifferenceHistogram differenceHistogram(const QImage &now, const QImage &then)
{
    DifferenceHistogram histogram{};
    for (int row{0}; row < now.height(); ++row) {
        const uchar *current{now.constScanLine(row)};
        const uchar *previous{then.constScanLine(row)};
        for (int col{0}; col < now.width(); ++col) {
            ++histogram.at(static_cast<size_t>(static_cast<int>(current[col]) -
                                               static_cast<int>(previous[col]) + 255));
        }
    }
    return histogram;
}

// The exposure shift is the median difference rather than the mean: a quarter
// of the picture turning bright moves the mean by a quarter of that and would
// make the other three quarters look changed, while the median stays with the
// part of the picture that did not move.
int medianShift(const DifferenceHistogram &histogram, long long count)
{
    long long seen{0};
    for (size_t bin{0}; bin < histogram.size(); ++bin) {
        seen += histogram.at(bin);
        if (seen * 2 >= count) {
            return static_cast<int>(bin) - 255;
        }
    }
    return 0;
}

// The median of the distance from `shift`, interpolated inside the bin it
// falls in. Grey levels are whole numbers, and the noise of an area averaged
// thumbnail is about one level, so the bin alone would round it to zero or to
// double its size.
double medianDeviation(const DifferenceHistogram &histogram, long long count, int shift)
{
    std::array<long long, 511> deviations{};
    for (size_t bin{0}; bin < histogram.size(); ++bin) {
        const int difference{static_cast<int>(bin) - 255};
        deviations.at(static_cast<size_t>(std::abs(difference - shift))) += histogram.at(bin);
    }
    const double half{static_cast<double>(count) / 2.0};
    long long below{0};
    for (size_t deviation{0}; deviation < deviations.size(); ++deviation) {
        const long long inside{deviations.at(deviation)};
        if (inside > 0 && static_cast<double>(below + inside) >= half) {
            // Deviation zero stands for [0, 0.5), every other one for the unit
            // interval around it.
            const double lower{deviation == 0 ? 0.0 : static_cast<double>(deviation) - 0.5};
            const double width{deviation == 0 ? 0.5 : 1.0};
            return lower +
                   (width * (half - static_cast<double>(below)) / static_cast<double>(inside));
        }
        below += inside;
    }
    return 0.0;
}

// The grey level below which `fraction` of the picture lies.
int percentile(const std::array<long long, 256> &levels, long long count, double fraction)
{
    const long long rank{std::max<long long>(1,
                                             static_cast<long long>(std::ceil(
                                                 fraction * static_cast<double>(count))))};
    long long seen{0};
    for (size_t level{0}; level < levels.size(); ++level) {
        seen += levels.at(level);
        if (seen >= rank) {
            return static_cast<int>(level);
        }
    }
    return 255;
}

int boundedThreshold(double threshold)
{
    return std::clamp(static_cast<int>(std::lround(threshold)),
                      MotionGate::minimumThreshold,
                      MotionGate::pixelThreshold);
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

void MotionGate::setReference(const QImage &thumbnail)
{
    m_reference = thumbnail;
    m_calibrated = false;
    m_contrastThreshold = pixelThreshold;
    if (isGrey(thumbnail)) {
        std::array<long long, 256> levels{};
        for (int row{0}; row < thumbnail.height(); ++row) {
            const uchar *line{thumbnail.constScanLine(row)};
            for (int col{0}; col < thumbnail.width(); ++col) {
                ++levels.at(line[col]);
            }
        }
        const long long count{pixelCount(thumbnail)};
        const int contrast{percentile(levels, count, 0.95) - percentile(levels, count, 0.05)};
        m_contrastThreshold = static_cast<double>(pixelThreshold) *
                              std::min(contrast, fullContrast) / fullContrast;
    }
    m_threshold = boundedThreshold(m_contrastThreshold);
}

void MotionGate::calibrate(const QImage &thumbnail)
{
    if (!comparable(thumbnail, m_reference)) {
        return;
    }
    const long long count{pixelCount(thumbnail)};
    const DifferenceHistogram histogram{differenceHistogram(thumbnail, m_reference)};
    const int shift{medianShift(histogram, count)};
    const double noise{noiseDeviations * deviationsPerMedianDeviation *
                       medianDeviation(histogram, count, shift)};
    // Rounded up, so the noise sits under the threshold rather than on it. The
    // upper bound keeps somebody walking in during the calibration from
    // blinding the gate until the next reference.
    m_threshold = boundedThreshold(std::max(m_contrastThreshold, std::ceil(noise)));
    m_calibrated = true;
}

void MotionGate::clear()
{
    m_reference = QImage{};
    m_calibrated = false;
    m_contrastThreshold = pixelThreshold;
    m_threshold = pixelThreshold;
}

double MotionGate::changedFraction(const QImage &thumbnail) const
{
    if (!comparable(thumbnail, m_reference)) {
        return 1.0;
    }

    const long long count{pixelCount(thumbnail)};
    const int shift{medianShift(differenceHistogram(thumbnail, m_reference), count)};

    long long changed{0};
    for (int row{0}; row < thumbnail.height(); ++row) {
        const uchar *now{thumbnail.constScanLine(row)};
        const uchar *then{m_reference.constScanLine(row)};
        for (int col{0}; col < thumbnail.width(); ++col) {
            const int difference{static_cast<int>(now[col]) - static_cast<int>(then[col]) - shift};
            if (std::abs(difference) >= m_threshold) {
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
