// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VIDEOFRAMEIMAGE_H
#define KERFUR_VIDEOFRAMEIMAGE_H

#include <QImage>
#include <QVideoFrame>

namespace Kerfur {

/// The frame as an image, or a null image when the frame cannot be read.
///
/// Packed RGB in memory is copied straight out of the mapped bytes. Everything
/// else (planar YUV, texture backed frames, and frames whose format asks for a
/// rotation or a flip) goes through `QVideoFrame::toImage`, which is the only
/// code that knows how to undo those.
///
/// `toImage` converts on the GPU, through an offscreen context it creates for
/// the calling thread. In a browser that is a second WebGL context beside the
/// one the scene graph draws with, while the browser backend only ever delivers
/// packed RGBA in memory, which needs no conversion at all.
///
/// A frame whose rows are shorter than its pixel format needs is labelled with
/// the wrong format. Reading it as that format would run off the end of every
/// row, so it comes back null.
QImage imageFromVideoFrame(const QVideoFrame &frame);

} // namespace Kerfur

#endif // KERFUR_VIDEOFRAMEIMAGE_H
