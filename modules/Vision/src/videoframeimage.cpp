// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "videoframeimage.h"

#include <QVideoFrameFormat>

namespace Kerfur {

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

} // namespace Kerfur
