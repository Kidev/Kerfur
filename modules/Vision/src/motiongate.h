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
///
/// The per pixel threshold follows the picture. A dark room spans a few tens of
/// grey levels, and somebody walking through it changes the picture by less
/// than a lit room's threshold, so the threshold shrinks with the reference's
/// contrast. A second thumbnail taken of the same empty room measures the
/// sensor's noise, and the threshold stays above it, so the gain a camera turns
/// up in the dark does not wake the models on its own.
class MotionGate
{
public:
    /// The thumbnail's width. Its height follows the frame's aspect.
    static constexpr int thumbnailWidth{128};
    /// The change in grey level, after the mean shift is taken out, from which
    /// a thumbnail pixel of a well lit picture counts as changed. Also the
    /// highest threshold: a gate is never less sensitive than this.
    static constexpr int pixelThreshold{16};
    /// The lowest threshold, whatever the contrast and the noise.
    static constexpr int minimumThreshold{3};
    /// The contrast, from the 5th to the 95th percentile of the reference's grey
    /// levels, at and above which `pixelThreshold` applies in full. Below it the
    /// threshold scales down in proportion.
    static constexpr int fullContrast{128};
    /// How many standard deviations of the measured noise a change must exceed.
    static constexpr double noiseDeviations{3.5};

    /// The thumbnail of a grey plane: `stride` bytes per row, one byte per
    /// pixel. The plane is read in place and never copied whole. Thumbnails are
    /// `QImage::Format_Grayscale8`, and null when the input cannot be read.
    static QImage thumbnail(const uchar *plane, int width, int height, qsizetype stride);
    /// The thumbnail of any image, converted to grey after the reduction.
    static QImage thumbnail(const QImage &image);

    /// Makes `thumbnail` the picture later ones are compared with, and sets the
    /// threshold from its contrast. The noise is unknown until `calibrate`.
    void setReference(const QImage &thumbnail);
    bool hasReference() const { return !m_reference.isNull(); }
    /// Measures the noise from `thumbnail`, a second picture of the scene the
    /// reference shows, and raises the threshold above it. A thumbnail that
    /// cannot be compared with the reference leaves the gate uncalibrated.
    void calibrate(const QImage &thumbnail);
    bool isCalibrated() const { return m_calibrated; }
    void clear();

    /// The change in grey level from which a pixel counts as changed.
    int threshold() const { return m_threshold; }

    /// The fraction of thumbnail pixels that changed, from 0 to 1. A thumbnail
    /// of another size or format than the reference, or no reference at all,
    /// counts as everything having changed.
    double changedFraction(const QImage &thumbnail) const;

private:
    QImage m_reference;
    double m_contrastThreshold{pixelThreshold};
    int m_threshold{pixelThreshold};
    bool m_calibrated{false};
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
