// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "videoframeimage.h"

#include <QVideoFrameFormat>

namespace Kerfur {

QImage imageFromVideoFrame(const QVideoFrame &source)
{
    if (!source.isValid()) {
        return {};
    }

    QVideoFrame frame{source};
    const QVideoFrameFormat surface{frame.surfaceFormat()};
    const QImage::Format format{
        QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat())};

    bool direct{format != QImage::Format_Invalid && frame.handleType() == QVideoFrame::NoHandle
                && !surface.isMirrored()
                && surface.scanLineDirection() == QVideoFrameFormat::TopToBottom};
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    direct = direct && surface.rotation() == QtVideo::Rotation::None;
#endif
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
