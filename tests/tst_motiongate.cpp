// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "motiongate.h"

#include <QImage>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <vector>

using namespace Kerfur;

namespace {

QImage greyFrame(int level, int width = 640, int height = 360)
{
    QImage image{width, height, QImage::Format_Grayscale8};
    image.fill(QColor{level, level, level});
    return image;
}

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
