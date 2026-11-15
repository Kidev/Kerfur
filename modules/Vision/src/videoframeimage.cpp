// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "videoframeimage.h"

#include <QTransform>
#include <QVideoFrameFormat>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WASM
#include <emscripten.h>

// The layout of the camera frame the page is showing at `width` x `height`,
// packed as the WebCodecs format code (low byte, numbered as
// `BrowserFrameLayout::Format`) | 0x100 for BT.709 | 0x200 for full range.
// Zero when no camera video of that size is playing, or when called off the
// main thread, where there is no document to look in.
EM_JS(int, kerfurBrowserFrameLayout, (int width, int height), {
    if (typeof document === "undefined" || typeof VideoFrame === "undefined") {
        return 0;
    }
    const formats = { I420: 1, NV12: 2, RGBA: 3, RGBX: 4, BGRA: 5, BGRX: 6 };
    for (const video of document.querySelectorAll("video.Camera")) {
        if (video.readyState < 2) {
            continue;
        }
        let frame = null;
        try {
            frame = new VideoFrame(video);
            if (frame.displayWidth === width && frame.displayHeight === height) {
                const space = frame.colorSpace || {};
                return (formats[frame.format] || 0) | (space.matrix === "bt709" ? 0x100 : 0)
                       | (space.fullRange === true ? 0x200 : 0);
            }
        } catch (error) {
            // No frame decoded yet; the next call asks again.
        } finally {
            if (frame !== null) {
                frame.close();
            }
        }
    }
    return 0;
})
#endif

namespace Kerfur {

namespace {

/// YUV to RGB in 16.16 fixed point, for one matrix and one range.
struct YuvToRgb
{
    int black{16};
    int luma{0};
    int redV{0};
    int greenU{0};
    int greenV{0};
    int blueU{0};
};

YuvToRgb yuvToRgb(const BrowserFrameLayout &layout)
{
    const double kr{layout.bt709 ? 0.2126 : 0.299};
    const double kb{layout.bt709 ? 0.0722 : 0.114};
    const double kg{1.0 - kr - kb};
    const double lumaScale{layout.fullRange ? 1.0 : 255.0 / 219.0};
    const double chromaScale{layout.fullRange ? 1.0 : 255.0 / 224.0};

    const auto fixed{[](double value) { return static_cast<int>(std::lround(value * 65536.0)); }};
    YuvToRgb matrix;
    matrix.black = layout.fullRange ? 0 : 16;
    matrix.luma = fixed(lumaScale);
    matrix.redV = fixed(2.0 * (1.0 - kr) * chromaScale);
    matrix.greenU = fixed(-2.0 * kb * (1.0 - kb) / kg * chromaScale);
    matrix.greenV = fixed(-2.0 * kr * (1.0 - kr) / kg * chromaScale);
    matrix.blueU = fixed(2.0 * (1.0 - kb) * chromaScale);
    return matrix;
}

uchar channel(int fixedValue)
{
    return static_cast<uchar>(std::clamp((fixedValue + 32768) >> 16, 0, 255));
}

/// I420 or NV12 as `copyTo` packs it: the luma plane, then either two chroma
/// planes of half the width and height or one of interleaved U and V.
QImage imageFromYuv420(const uchar *bytes,
                       qsizetype size,
                       const QSize &frameSize,
                       qsizetype stride,
                       bool interleaved,
                       const YuvToRgb &matrix)
{
    const int width{frameSize.width()};
    const int height{frameSize.height()};
    const qsizetype chromaWidth{(static_cast<qsizetype>(width) + 1) / 2};
    const qsizetype chromaHeight{(static_cast<qsizetype>(height) + 1) / 2};
    const qsizetype lumaBytes{static_cast<qsizetype>(width) * height};
    if (stride != width || size < lumaBytes + (2 * chromaWidth * chromaHeight)) {
        return {};
    }

    QImage image{frameSize, QImage::Format_RGBX8888};
    if (image.isNull()) {
        return {};
    }

    const uchar *const firstChroma{bytes + lumaBytes};
    const uchar *const secondChroma{firstChroma + (chromaWidth * chromaHeight)};
    const qsizetype chromaStride{interleaved ? 2 * chromaWidth : chromaWidth};
    for (int y{0}; y < height; ++y) {
        const uchar *const lumaRow{bytes + (static_cast<qsizetype>(y) * width)};
        const qsizetype chromaRow{(static_cast<qsizetype>(y) / 2) * chromaStride};
        uchar *out{image.scanLine(y)};
        for (int x{0}; x < width; ++x) {
            const qsizetype column{x / 2};
            const int u{(interleaved ? firstChroma[chromaRow + (2 * column)]
                                     : firstChroma[chromaRow + column])
                        - 128};
            const int v{(interleaved ? firstChroma[chromaRow + (2 * column) + 1]
                                     : secondChroma[chromaRow + column])
                        - 128};
            const int luma{(lumaRow[x] - matrix.black) * matrix.luma};
            out[0] = channel(luma + (matrix.redV * v));
            out[1] = channel(luma + (matrix.greenU * u) + (matrix.greenV * v));
            out[2] = channel(luma + (matrix.blueU * u));
            out[3] = 255;
            out += 4;
        }
    }
    return image;
}

/// Four bytes per pixel in RGBA or BGRA order, the fourth meaningful or not.
QImage imageFromPacked(const uchar *bytes,
                       qsizetype size,
                       const QSize &frameSize,
                       qsizetype stride,
                       bool swapped,
                       bool opaque)
{
    if (stride < static_cast<qsizetype>(frameSize.width()) * 4
        || size < stride * frameSize.height()) {
        return {};
    }
    // Converted or copied, because the bytes belong to the frame and are gone
    // once it is unmapped. RGBX promises an opaque fourth byte, which only the
    // conversion keeps.
    const QImage view{
        bytes, frameSize.width(), frameSize.height(), stride, QImage::Format_RGBA8888};
    QImage image{opaque ? view.convertToFormat(QImage::Format_RGBX8888) : view.copy()};
    if (swapped) {
        image.rgbSwap();
    }
    return image;
}

#ifdef Q_OS_WASM
BrowserFrameLayout browserFrameLayout(const QSize &frameSize)
{
    const int code{kerfurBrowserFrameLayout(frameSize.width(), frameSize.height())};
    const int format{code & 0xff};
    BrowserFrameLayout layout;
    if (format > 0 && format <= static_cast<int>(BrowserFrameLayout::Format::Bgrx)) {
        layout.format = static_cast<BrowserFrameLayout::Format>(format);
    }
    layout.bt709 = (code & 0x100) != 0;
    layout.fullRange = (code & 0x200) != 0;
    return layout;
}
#endif

} // namespace

PlaneFill firstPlaneFill(const QVideoFrame &source)
{
    if (!source.isValid() || source.handleType() != QVideoFrame::NoHandle
        || source.pixelFormat() == QVideoFrameFormat::Format_Jpeg
        || source.pixelFormat() == QVideoFrameFormat::Format_Invalid) {
        return {};
    }

    QVideoFrame frame{source};
    if (!frame.map(QVideoFrame::ReadOnly)) {
        return {};
    }
    const qsizetype stride{frame.bytesPerLine(0)};
    const PlaneFill fill{static_cast<qsizetype>(frame.mappedBytes(0)),
                         stride > 0 ? stride * frame.height() : 0};
    frame.unmap();
    return fill;
}

QImage imageFromVideoFrame(const QVideoFrame &source)
{
    if (!source.isValid()) {
        return {};
    }

    // Before either path below: the converter trusts the stride and height and
    // reads that many bytes whatever the frame holds.
    const PlaneFill fill{firstPlaneFill(source)};
    if (fill.held < fill.needed) {
        return {};
    }

#ifdef Q_OS_WASM
    // Every frame Qt copies out of a WebCodecs VideoFrame is labelled RGBA.
    // One that does not fit the layout the page reports falls through to the
    // reading below, which refuses it if the label is wrong too.
    if (source.pixelFormat() == QVideoFrameFormat::Format_RGBA8888
        && source.handleType() == QVideoFrame::NoHandle) {
        const BrowserFrameLayout layout{browserFrameLayout(source.size())};
        if (layout.format != BrowserFrameLayout::Format::Unknown) {
            QImage image{imageFromBrowserFrame(source, layout)};
            if (!image.isNull()) {
                return image;
            }
        }
    }
#endif

    QVideoFrame frame{source};
    const QVideoFrameFormat surface{frame.surfaceFormat()};
    const QImage::Format format{
        QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat())};

    const bool direct{format != QImage::Format_Invalid
                      && frame.handleType() == QVideoFrame::NoHandle && !surface.isMirrored()
                      && surface.scanLineDirection() == QVideoFrameFormat::TopToBottom
                      && surface.rotation() == QtVideo::Rotation::None};
    if (!direct || !frame.map(QVideoFrame::ReadOnly)) {
        return frame.toImage();
    }

    QImage image;
    const int width{frame.width()};
    const int height{frame.height()};
    const qsizetype stride{frame.bytesPerLine(0)};
    const qsizetype rowBytes{static_cast<qsizetype>(width)
                             * QImage::toPixelFormat(format).bitsPerPixel() / 8};
    if (frame.bits(0) != nullptr && width > 0 && height > 0 && stride >= rowBytes
        && static_cast<qsizetype>(frame.mappedBytes(0)) >= stride * height) {
        // Copied, because the bytes belong to the frame and are gone once it
        // is unmapped.
        image = QImage{frame.bits(0), width, height, stride, format}.copy();
    }
    frame.unmap();
    return image;
}

QImage imageFromBrowserFrame(const QVideoFrame &source, const BrowserFrameLayout &layout)
{
    if (!source.isValid() || source.handleType() != QVideoFrame::NoHandle
        || layout.format == BrowserFrameLayout::Format::Unknown || source.size().isEmpty()) {
        return {};
    }
    const QVideoFrameFormat surface{source.surfaceFormat()};
    if (surface.scanLineDirection() != QVideoFrameFormat::TopToBottom) {
        return {};
    }

    QVideoFrame frame{source};
    if (!frame.map(QVideoFrame::ReadOnly)) {
        return {};
    }
    const uchar *const bytes{frame.bits(0)};
    const qsizetype size{static_cast<qsizetype>(frame.mappedBytes(0))};
    const qsizetype stride{frame.bytesPerLine(0)};

    QImage image;
    if (bytes != nullptr) {
        switch (layout.format) {
        case BrowserFrameLayout::Format::I420:
        case BrowserFrameLayout::Format::Nv12:
            image = imageFromYuv420(bytes,
                                    size,
                                    frame.size(),
                                    stride,
                                    layout.format == BrowserFrameLayout::Format::Nv12,
                                    yuvToRgb(layout));
            break;
        case BrowserFrameLayout::Format::Rgba:
        case BrowserFrameLayout::Format::Rgbx:
        case BrowserFrameLayout::Format::Bgra:
        case BrowserFrameLayout::Format::Bgrx:
            image = imageFromPacked(bytes,
                                    size,
                                    frame.size(),
                                    stride,
                                    layout.format == BrowserFrameLayout::Format::Bgra
                                        || layout.format == BrowserFrameLayout::Format::Bgrx,
                                    layout.format == BrowserFrameLayout::Format::Rgbx
                                        || layout.format == BrowserFrameLayout::Format::Bgrx);
            break;
        case BrowserFrameLayout::Format::Unknown:
            break;
        }
    }
    frame.unmap();

    // In the order Qt's own converter applies them: rotation, then the
    // horizontal flip.
    QTransform transform;
    if (surface.rotation() != QtVideo::Rotation::None) {
        transform.rotate(static_cast<qreal>(surface.rotation()));
    }
    if (surface.isMirrored()) {
        transform.scale(-1.0, 1.0);
    }
    if (!image.isNull() && !transform.isIdentity()) {
        image = image.transformed(transform);
    }
    return image;
}

} // namespace Kerfur
