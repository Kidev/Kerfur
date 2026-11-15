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
/// one the scene graph draws with, so browser frames never reach it: the page
/// is asked for their real layout (see `BrowserFrameLayout`) and they are read
/// with `imageFromBrowserFrame`.
///
/// A frame whose rows are shorter than its pixel format needs is labelled with
/// the wrong format. Reading it as that format would run off the end of every
/// row, so it comes back null. So does a frame that holds fewer bytes than its
/// size and stride need (a camera that delivers part of each frame), which
/// would otherwise be converted from whatever memory follows it.
QImage imageFromVideoFrame(const QVideoFrame &frame);

/// How the bytes of one browser camera frame are laid out, as WebCodecs
/// reports it on the `VideoFrame` they were copied from.
///
/// Qt's browser backend copies each frame with `VideoFrame.copyTo`, which
/// writes the frame's own layout, and labels the copy RGBA whatever that
/// layout was. I420 and NV12 then arrive with a first plane stride of one byte
/// per pixel, and BGRA with red and blue swapped, so the label alone cannot be
/// read.
struct BrowserFrameLayout
{
    enum class Format : quint8 { Unknown, I420, Nv12, Rgba, Rgbx, Bgra, Bgrx };

    Format format{Format::Unknown};
    /// BT.709 coefficients rather than BT.601.
    bool bt709{false};
    /// Samples span 0 to 255 rather than 16 to 235 (16 to 240 for chroma).
    bool fullRange{false};
};

/// The frame's bytes read in `layout`, with the frame's rotation and mirroring
/// applied, or a null image when they do not fit it. Planar YUV must be packed
/// the way `copyTo` writes it by default (a first plane stride equal to the
/// width, each plane straight after the one before); packed RGB needs four
/// bytes per pixel in every row.
QImage imageFromBrowserFrame(const QVideoFrame &frame, const BrowserFrameLayout &layout);

/// How many bytes the first plane of a frame in memory holds, and how many its
/// stride and height need. Every uncompressed format keeps a full height first
/// plane, so `held < needed` means the frame is incomplete. Both are zero for a
/// frame that is invalid, compressed, texture backed or cannot be mapped.
struct PlaneFill
{
    qsizetype held{0};
    qsizetype needed{0};
};
PlaneFill firstPlaneFill(const QVideoFrame &frame);

} // namespace Kerfur

#endif // KERFUR_VIDEOFRAMEIMAGE_H
