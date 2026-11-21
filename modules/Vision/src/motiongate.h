// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_MOTIONGATE_H
#define KERFUR_MOTIONGATE_H

#include <QImage>

namespace Kerfur {

/// Says how much of the picture changed since a reference picture, on a
/// thumbnail small enough that measuring it costs less than converting the
/// frame to colour.
///
/// The thumbnail is an area average, so sensor noise mostly cancels before
/// anything is compared. A change of exposure or of the room light moves every
/// pixel at once; the median of the difference is subtracted before the per
/// pixel threshold, so that only a part of the picture changing counts.
class MotionGate
{
public:
    /// The thumbnail's width. Its height follows the frame's aspect.
    static constexpr int thumbnailWidth{128};
    /// The change in grey level, after the mean shift is taken out, from which
    /// a thumbnail pixel counts as changed.
    static constexpr int pixelThreshold{16};

    /// The thumbnail of a grey plane: `stride` bytes per row, one byte per
    /// pixel. The plane is read in place and never copied whole. Thumbnails are
    /// `QImage::Format_Grayscale8`, and null when the input cannot be read.
    static QImage thumbnail(const uchar *plane, int width, int height, qsizetype stride);
    /// The thumbnail of any image, converted to grey after the reduction.
    static QImage thumbnail(const QImage &image);

    /// Makes `thumbnail` the picture later ones are compared with.
    void setReference(const QImage &thumbnail) { m_reference = thumbnail; }
    bool hasReference() const { return !m_reference.isNull(); }
    void clear() { m_reference = QImage{}; }

    /// The fraction of thumbnail pixels that changed, from 0 to 1. A thumbnail
    /// of another size or format than the reference, or no reference at all,
    /// counts as everything having changed.
    double changedFraction(const QImage &thumbnail) const;

private:
    QImage m_reference;
};

/// When the models stop for lack of anybody to look at.
///
/// While running, every pass reports whether it found somebody. After
/// `idleAfterMs` without anybody the clock starts watching, and the models
/// stop. Motion `wake`s it, and the models then have only `probeMs` to find
/// somebody before watching resumes; finding somebody restores the full idle
/// period. Times are milliseconds on any monotonic clock the caller keeps.
class IdleClock
{
public:
    void setLimits(qint64 idleAfterMs, qint64 probeMs);
    /// Disabled, the clock never starts watching, and stops if it was.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isWatching() const { return m_watching; }

    /// The models are running from `now`, with the full idle period ahead.
    void reset(qint64 now);
    /// Motion woke the models at `now`.
    void wake(qint64 now);
    /// One pass of the models at `now`. True when it starts watching.
    bool notePass(bool present, qint64 now);

private:
    qint64 m_idleAfterMs{30'000};
    qint64 m_probeMs{3'000};
    qint64 m_since{0};
    bool m_enabled{true};
    bool m_watching{false};
    bool m_probing{false};
};

} // namespace Kerfur

#endif // KERFUR_MOTIONGATE_H
