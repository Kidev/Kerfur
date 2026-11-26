// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "absenceclock.h"

#include <QTest>

using namespace Kerfur;

namespace {

constexpr qint64 hour{3'600'000};
const QString alice{QStringLiteral("alice")};
const QString bob{QStringLiteral("bob")};

} // namespace

/// The time each person has been out of sight, in waking time, which is what
/// decides whether Kerfur greets them when they come back.
class TestAbsenceClock : public QObject
{
    Q_OBJECT

private slots:
    void aStrangerToTheClockWasNeverSeen();
    void awakeTimeCountsOnce();
    void asleepTimeCountsFactorTimesOver();
    void aNapInsideTheDayAddsItsShare();
    void aFactorBelowOneRunsAtTheWakingRate();
    void eachPersonHasTheirOwnAbsence();
    void saveLeavesOutTheLongAbsent();
    void restoreAddsTheTimeOffline();
    void restoreSkipsWhatIsNotADuration();
    void reunionNeedsTheFullAbsence();
};

void TestAbsenceClock::aStrangerToTheClockWasNeverSeen()
{
    AbsenceClock clock;
    QCOMPARE(clock.absence(alice, 10 * hour), qint64{-1});
    QCOMPARE(clock.see(alice, 10 * hour), qint64{-1});
    QCOMPARE(clock.absence(alice, 10 * hour), qint64{0});
}

void TestAbsenceClock::awakeTimeCountsOnce()
{
    AbsenceClock clock;
    clock.see(alice, 0);
    QCOMPARE(clock.absence(alice, 3 * hour), 3 * hour);
    QCOMPARE(clock.see(alice, 3 * hour), 3 * hour);
    QCOMPARE(clock.absence(alice, 3 * hour), qint64{0});
}

/// Two hours asleep at the default factor of four are a full eight.
void TestAbsenceClock::asleepTimeCountsFactorTimesOver()
{
    AbsenceClock clock;
    clock.setSleepFactor(4.0, 0);
    clock.see(alice, 0);
    clock.setAsleep(true, 0);
    QCOMPARE(clock.absence(alice, 2 * hour), 8 * hour);
    clock.setAsleep(false, 2 * hour);
    QCOMPARE(clock.absence(alice, 3 * hour), 9 * hour);
}

void TestAbsenceClock::aNapInsideTheDayAddsItsShare()
{
    AbsenceClock clock;
    clock.setSleepFactor(4.0, 0);
    clock.see(alice, 0);
    clock.setAsleep(true, 1 * hour);
    clock.setAsleep(false, 2 * hour);
    // One hour awake, one asleep worth four, one awake.
    QCOMPARE(clock.see(alice, 3 * hour), 6 * hour);
}

void TestAbsenceClock::aFactorBelowOneRunsAtTheWakingRate()
{
    AbsenceClock clock;
    clock.setSleepFactor(0.25, 0);
    QCOMPARE(clock.sleepFactor(), 1.0);
    clock.see(alice, 0);
    clock.setAsleep(true, 0);
    QCOMPARE(clock.absence(alice, 2 * hour), 2 * hour);
}

void TestAbsenceClock::eachPersonHasTheirOwnAbsence()
{
    AbsenceClock clock;
    clock.see(alice, 0);
    clock.see(bob, 5 * hour);
    QCOMPARE(clock.absence(alice, 6 * hour), 6 * hour);
    QCOMPARE(clock.absence(bob, 6 * hour), 1 * hour);
}

void TestAbsenceClock::saveLeavesOutTheLongAbsent()
{
    AbsenceClock clock;
    clock.see(alice, 0);
    clock.see(bob, 7 * hour);
    const QVariantMap saved{clock.save(9 * hour, 8 * hour)};
    QVERIFY(!saved.contains(alice));
    QCOMPARE(saved.value(bob).toLongLong(), 2 * hour);
}

void TestAbsenceClock::restoreAddsTheTimeOffline()
{
    AbsenceClock before;
    before.see(alice, 0);
    const QVariantMap saved{before.save(1 * hour, 8 * hour)};

    AbsenceClock after;
    after.restore(saved, 5 * hour, 0);
    QCOMPARE(after.absence(alice, 0), 6 * hour);
    QCOMPARE(after.absence(alice, 2 * hour), 8 * hour);

    // A wall clock that went backwards adds nothing.
    AbsenceClock rewound;
    rewound.restore(saved, -5 * hour, 0);
    QCOMPARE(rewound.absence(alice, 0), 1 * hour);
}

void TestAbsenceClock::restoreSkipsWhatIsNotADuration()
{
    AbsenceClock clock;
    clock.restore(QVariantMap{{alice, QStringLiteral("soon")},
                              {bob, QVariant::fromValue<qlonglong>(-1)}},
                  0,
                  0);
    QVERIFY(clock.identities().isEmpty());
}

void TestAbsenceClock::reunionNeedsTheFullAbsence()
{
    QVERIFY(isReunion(-1, 8 * hour));
    QVERIFY(isReunion(8 * hour, 8 * hour));
    QVERIFY(!isReunion((8 * hour) - 1, 8 * hour));
    QVERIFY(!isReunion(0, 8 * hour));
}

QTEST_APPLESS_MAIN(TestAbsenceClock)

#include "tst_absenceclock.moc"
