// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "blackboard.h"

#include <QSignalSpy>
#include <QTest>

using namespace Kerfur;

class TestBlackboard : public QObject
{
    Q_OBJECT

private slots:
    void declaresAValueWithADefault();
    void keepsAValueThatWasAlreadySet();
    void addsToANumber();
    void resetsToTheDeclaredDefault();
    void removesAnUndeclaredKeyOnReset();
    void announcesEveryChangeOnce();
    void staysQuietWhenNothingChanged();
    void expandsBothPlaceholderSpellings();
    void prefersSuppliedValuesOverStoredOnes();
    void leavesAnUnknownPlaceholderAlone();
    void exposesValuesThroughTheMap();
    void snapshotsEverything();
};

void TestBlackboard::declaresAValueWithADefault()
{
    Blackboard board{nullptr};
    board.declare(QStringLiteral("count"), 0);
    QCOMPARE(board.number(QStringLiteral("count")), 0.0);
    QVERIFY(board.keys().contains(QStringLiteral("count")));
}

void TestBlackboard::keepsAValueThatWasAlreadySet()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("count"), 42);
    // Declaring afterwards is what a restored value meets when the skill that
    // owns it loads. Yesterday's count must not be replaced by the default.
    board.declare(QStringLiteral("count"), 0);
    QCOMPARE(board.number(QStringLiteral("count")), 42.0);
}

void TestBlackboard::addsToANumber()
{
    Blackboard board{nullptr};
    board.declare(QStringLiteral("count"), 0);
    QCOMPARE(board.add(QStringLiteral("count"), 300), 300.0);
    QCOMPARE(board.add(QStringLiteral("count"), 150), 450.0);
    QCOMPARE(board.number(QStringLiteral("count")), 450.0);
}

void TestBlackboard::resetsToTheDeclaredDefault()
{
    Blackboard board{nullptr};
    board.declare(QStringLiteral("count"), 7);
    board.set(QStringLiteral("count"), 900);
    board.reset(QStringLiteral("count"));
    QCOMPARE(board.number(QStringLiteral("count")), 7.0);
}

void TestBlackboard::removesAnUndeclaredKeyOnReset()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("scratch"), 1);
    board.reset(QStringLiteral("scratch"));
    QVERIFY(!board.value(QStringLiteral("scratch")).isValid());
}

void TestBlackboard::announcesEveryChangeOnce()
{
    Blackboard board{nullptr};
    QSignalSpy spy{&board, &Blackboard::changed};
    board.set(QStringLiteral("count"), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("count"));
    board.set(QStringLiteral("count"), 2);
    QCOMPARE(spy.count(), 2);
}

void TestBlackboard::staysQuietWhenNothingChanged()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("count"), 1);
    QSignalSpy spy{&board, &Blackboard::changed};
    board.set(QStringLiteral("count"), 1);
    QCOMPARE(spy.count(), 0);
}

void TestBlackboard::expandsBothPlaceholderSpellings()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("caloriesOfTheDay"), 450);
    board.set(QStringLiteral("maxCaloriesPerDay"), 2000);
    QCOMPARE(board.expand(QStringLiteral("You are at <caloriesOfTheDay> of {maxCaloriesPerDay}")),
             QStringLiteral("You are at 450 of 2000"));
}

void TestBlackboard::prefersSuppliedValuesOverStoredOnes()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("amount"), 1);
    // The confirmation question has to read the amount from the request being
    // confirmed, which is precisely the value not yet written down.
    QCOMPARE(board.expand(QStringLiteral("set it to {amount}?"),
                          {{QStringLiteral("amount"), 2500}}),
             QStringLiteral("set it to 2500?"));
}

void TestBlackboard::leavesAnUnknownPlaceholderAlone()
{
    Blackboard board{nullptr};
    QCOMPARE(board.expand(QStringLiteral("hello {nobody}")), QStringLiteral("hello {nobody}"));
}

void TestBlackboard::exposesValuesThroughTheMap()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("count"), 5);
    QVERIFY(board.values() != nullptr);
    QCOMPARE(board.values()->value(QStringLiteral("count")).toInt(), 5);
}

void TestBlackboard::snapshotsEverything()
{
    Blackboard board{nullptr};
    board.set(QStringLiteral("a"), 1);
    board.set(QStringLiteral("b"), QStringLiteral("two"));
    const QVariantMap snapshot{board.snapshot()};
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot.value(QStringLiteral("b")).toString(), QStringLiteral("two"));
}

QTEST_MAIN(TestBlackboard)

#include "tst_blackboard.moc"
