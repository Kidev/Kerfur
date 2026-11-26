// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "motiongate.h"

#include <QImage>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

using namespace Kerfur;

namespace {

QImage greyFrame(int level, int width = 640, int height = 360)
{
    QImage image{width, height, QImage::Format_Grayscale8};
    image.fill(QColor{level, level, level});
    return image;
}

/// A thumbnail whose columns run from `left` to `right`: a room with a dark
/// side and a lit side, and a contrast of about their difference.
QImage gradientThumbnail(int left, int right)
{
    QImage image{MotionGate::thumbnailWidth, 72, QImage::Format_Grayscale8};
    for (int row{0}; row < image.height(); ++row) {
        uchar *line{image.scanLine(row)};
        for (int col{0}; col < image.width(); ++col) {
            line[col] = static_cast<uchar>(std::lround(
                left + (static_cast<double>(right - left) * col / (image.width() - 1))));
        }
    }
    return image;
}

/// `image` with independent normal noise of `deviation` grey levels on every
/// pixel, seeded so that a run is repeatable.
QImage withNoise(const QImage &image, double deviation, unsigned seed)
{
    QImage noisy{image};
    std::mt19937 generator{seed};
    std::normal_distribution<double> noise{0.0, deviation};
    for (int row{0}; row < noisy.height(); ++row) {
        uchar *line{noisy.scanLine(row)};
        for (int col{0}; col < noisy.width(); ++col) {
            line[col] = static_cast<uchar>(
                std::clamp(static_cast<int>(std::lround(line[col] + noise(generator))), 0, 255));
        }
    }
    return noisy;
}

/// `image` with `area` raised by `delta` grey levels: somebody, lit a little
/// more than what they stand in front of.
QImage withFigure(const QImage &image, const QRect &area, int delta)
{
    QImage changed{image};
    for (int row{area.top()}; row <= area.bottom(); ++row) {
        uchar *line{changed.scanLine(row)};
        for (int col{area.left()}; col <= area.right(); ++col) {
            line[col] = static_cast<uchar>(std::clamp(line[col] + delta, 0, 255));
        }
    }
    return changed;
}

// The share of the picture that wakes the models by default.
constexpr double wakeFraction{0.01};

} // namespace

/// The crude motion detector idle watching runs on, and the clock that decides
/// when idle watching starts and stops.
class TestMotionGate : public QObject
{
    Q_OBJECT

private slots:
    void thumbnailKeepsTheAspect();
    void colourAndGreyThumbnailsAgree();
    void thumbnailReadsAStridedPlane();

    void sameFrameHasNotChanged();
    void movedRegionIsMeasured();
    void wholeFrameBrightnessShiftIsIgnored();
    void noReferenceCountsAsChange();
    void otherSizeCountsAsChange();
    void aLitRoomKeepsTheFullThreshold();
    void movementInTheDarkIsSeen();
    void aCloseUpInTheDarkIsSeen();
    void sensorNoiseDoesNotWake();
    void movementStandsOutOfTheNoise();
    void calibrationNeverBlindsTheGate();

    void presenceKeepsTheModelsRunning();
    void absenceStartsWatching();
    void motionGivesOnlyTheProbePeriod();
    void somebodyFoundAfterMotionRestoresTheIdlePeriod();
    void disabledClockNeverWatches();
};

void TestMotionGate::thumbnailKeepsTheAspect()
{
    const QImage thumbnail{MotionGate::thumbnail(greyFrame(100, 1920, 1080))};
    QCOMPARE(thumbnail.width(), MotionGate::thumbnailWidth);
    QCOMPARE(thumbnail.height(), 72);
    QCOMPARE(thumbnail.format(), QImage::Format_Grayscale8);
}

void TestMotionGate::colourAndGreyThumbnailsAgree()
{
    QImage colour{640, 360, QImage::Format_RGB888};
    colour.fill(QColor{120, 120, 120});
    const QImage fromColour{MotionGate::thumbnail(colour)};
    const QImage fromGrey{MotionGate::thumbnail(greyFrame(120))};
    QCOMPARE(fromColour.size(), fromGrey.size());
    QVERIFY(qAbs(qGray(fromColour.pixel(10, 10)) - qGray(fromGrey.pixel(10, 10))) <= 1);

    QImage words{640, 360, QImage::Format_RGB32};
    words.fill(QColor{120, 120, 120});
    QVERIFY(qAbs(qGray(MotionGate::thumbnail(words).pixel(10, 10)) - 120) <= 1);
}

void TestMotionGate::thumbnailReadsAStridedPlane()
{
    // Rows padded to 704 bytes, the padding a different level from the
    // picture: it must never reach the thumbnail.
    constexpr int width{640};
    constexpr int height{360};
    constexpr int stride{704};
    std::vector<uchar> plane(static_cast<size_t>(stride) * height, uchar{10});
    for (int row{0}; row < height; ++row) {
        std::fill_n(plane.begin() + static_cast<std::ptrdiff_t>(row) * stride, width, uchar{200});
    }
    const QImage thumbnail{MotionGate::thumbnail(plane.data(), width, height, stride)};
    QCOMPARE(thumbnail.width(), MotionGate::thumbnailWidth);
    QCOMPARE(qGray(thumbnail.pixel(thumbnail.width() - 1, 0)), 200);
}

void TestMotionGate::sameFrameHasNotChanged()
{
    MotionGate gate;
    gate.setReference(MotionGate::thumbnail(greyFrame(80)));
    QCOMPARE(gate.changedFraction(MotionGate::thumbnail(greyFrame(80))), 0.0);
}

void TestMotionGate::movedRegionIsMeasured()
{
    MotionGate gate;
    gate.setReference(MotionGate::thumbnail(greyFrame(80)));

    // A quarter of the frame turns bright, as somebody walking in would.
    QImage frame{greyFrame(80)};
    for (int row{0}; row < 180; ++row) {
        std::fill_n(frame.scanLine(row), 320, uchar{230});
    }
    const double changed{gate.changedFraction(MotionGate::thumbnail(frame))};
    QVERIFY2(changed > 0.2 && changed < 0.3, qPrintable(QString::number(changed)));
}

void TestMotionGate::wholeFrameBrightnessShiftIsIgnored()
{
    MotionGate gate;
    gate.setReference(MotionGate::thumbnail(greyFrame(80)));
    QCOMPARE(gate.changedFraction(MotionGate::thumbnail(greyFrame(140))), 0.0);
}

void TestMotionGate::noReferenceCountsAsChange()
{
    const MotionGate gate;
    QVERIFY(!gate.hasReference());
    QCOMPARE(gate.changedFraction(MotionGate::thumbnail(greyFrame(80))), 1.0);
}

void TestMotionGate::otherSizeCountsAsChange()
{
    MotionGate gate;
    gate.setReference(MotionGate::thumbnail(greyFrame(80, 640, 360)));
    QCOMPARE(gate.changedFraction(MotionGate::thumbnail(greyFrame(80, 640, 480))), 1.0);
}

void TestMotionGate::aLitRoomKeepsTheFullThreshold()
{
    MotionGate gate;
    const QImage room{gradientThumbnail(0, 255)};
    gate.setReference(room);
    QCOMPARE(gate.threshold(), MotionGate::pixelThreshold);
    gate.calibrate(room);
    QVERIFY(gate.isCalibrated());
    QCOMPARE(gate.threshold(), MotionGate::pixelThreshold);
}

/// A dark room spans thirty grey levels, and somebody walking through it
/// brightens what they cover by eight: half of a lit room's threshold.
void TestMotionGate::movementInTheDarkIsSeen()
{
    MotionGate gate;
    const QImage room{gradientThumbnail(10, 40)};
    gate.setReference(room);
    gate.calibrate(room);
    QVERIFY(gate.threshold() < 8);

    const double changed{gate.changedFraction(withFigure(room, QRect{32, 10, 40, 50}, 8))};
    QVERIFY2(changed > 0.15, qPrintable(QString::number(changed)));
}

/// Close enough to fill most of the picture, the figure is the median and the
/// exposure correction takes it out; what is left is the room behind, which
/// now looks changed by as much.
void TestMotionGate::aCloseUpInTheDarkIsSeen()
{
    MotionGate gate;
    const QImage room{gradientThumbnail(10, 40)};
    gate.setReference(room);
    gate.calibrate(room);

    const double changed{gate.changedFraction(withFigure(room, QRect{0, 0, 90, 72}, 8))};
    QVERIFY2(changed > wakeFraction, qPrintable(QString::number(changed)));
}

/// A camera turns its gain up in the dark, and the noise it adds would reach
/// a threshold scaled to the contrast alone on a third of the picture.
void TestMotionGate::sensorNoiseDoesNotWake()
{
    const QImage room{gradientThumbnail(10, 40)};
    constexpr double noise{2.5};

    MotionGate gate;
    gate.setReference(withNoise(room, noise, 1));
    gate.calibrate(withNoise(room, noise, 2));
    QVERIFY(gate.isCalibrated());

    const double changed{gate.changedFraction(withNoise(room, noise, 3))};
    QVERIFY2(changed < wakeFraction, qPrintable(QString::number(changed)));
}

void TestMotionGate::movementStandsOutOfTheNoise()
{
    const QImage room{gradientThumbnail(10, 40)};
    constexpr double noise{2.5};

    MotionGate gate;
    gate.setReference(withNoise(room, noise, 4));
    gate.calibrate(withNoise(room, noise, 5));

    const QImage frame{withFigure(withNoise(room, noise, 6), QRect{0, 0, 64, 36}, 20)};
    const double changed{gate.changedFraction(frame)};
    QVERIFY2(changed > 0.15, qPrintable(QString::number(changed)));
}

/// Somebody crossing the room while the noise is measured looks like a very
/// noisy camera. The gate is then as sensitive as a lit room's, and no less.
void TestMotionGate::calibrationNeverBlindsTheGate()
{
    const QImage room{gradientThumbnail(0, 255)};

    MotionGate gate;
    gate.setReference(room);
    gate.calibrate(withNoise(room, 30.0, 7));
    QVERIFY(gate.isCalibrated());
    QCOMPARE(gate.threshold(), MotionGate::pixelThreshold);

    const double changed{gate.changedFraction(withFigure(room, QRect{0, 0, 64, 36}, 40))};
    QVERIFY2(changed > 0.2, qPrintable(QString::number(changed)));
}

void TestMotionGate::presenceKeepsTheModelsRunning()
{
    IdleClock clock;
    clock.setLimits(30'000, 3'000);
    clock.reset(0);
    QVERIFY(!clock.notePass(true, 29'000));
    QVERIFY(!clock.notePass(false, 50'000));
    QVERIFY(!clock.isWatching());
}

void TestMotionGate::absenceStartsWatching()
{
    IdleClock clock;
    clock.setLimits(30'000, 3'000);
    clock.reset(0);
    QVERIFY(!clock.notePass(false, 29'999));
    QVERIFY(clock.notePass(false, 30'000));
    QVERIFY(clock.isWatching());
    // Watching already: a further pass does not start it again.
    QVERIFY(!clock.notePass(false, 31'000));
}

void TestMotionGate::motionGivesOnlyTheProbePeriod()
{
    IdleClock clock;
    clock.setLimits(30'000, 3'000);
    clock.reset(0);
    QVERIFY(clock.notePass(false, 30'000));
    clock.wake(40'000);
    QVERIFY(!clock.isWatching());
    QVERIFY(!clock.notePass(false, 42'999));
    QVERIFY(clock.notePass(false, 43'000));
}

void TestMotionGate::somebodyFoundAfterMotionRestoresTheIdlePeriod()
{
    IdleClock clock;
    clock.setLimits(30'000, 3'000);
    clock.reset(0);
    clock.wake(1'000);
    QVERIFY(!clock.notePass(true, 2'000));
    QVERIFY(!clock.notePass(false, 10'000));
    QVERIFY(!clock.notePass(false, 31'999));
    QVERIFY(clock.notePass(false, 32'000));
}

void TestMotionGate::disabledClockNeverWatches()
{
    IdleClock clock;
    clock.setLimits(30'000, 3'000);
    clock.reset(0);
    QVERIFY(clock.notePass(false, 30'000));
    clock.setEnabled(false);
    QVERIFY(!clock.isWatching());
    clock.reset(30'000);
    QVERIFY(!clock.notePass(false, 1'000'000));
    QVERIFY(!clock.isWatching());
}

QTEST_APPLESS_MAIN(TestMotionGate)

#include "tst_motiongate.moc"
