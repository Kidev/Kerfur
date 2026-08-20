// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "intent.h"

#include <QTest>

using namespace Kerfur;

namespace {

/// The three triggers from `skills/calories.qml`, which is the worked example
/// and therefore the thing that has to keep working.
QList<IntentSpec> calorieIntents()
{
    return {
        IntentSpec{QStringLiteral("calories::add"),
                   QStringLiteral("when asked about adding calories"),
                   {QStringLiteral("add {amount} calories"),
                    QStringLiteral("i ate {amount} calories"),
                    QStringLiteral("log {amount} calories")},
                   QStringLiteral("calories")},
        IntentSpec{QStringLiteral("calories::limit"),
                   QStringLiteral("when asked to set the number of calories per day"),
                   {QStringLiteral("set my daily calorie limit to {amount}"),
                    QStringLiteral("set the number of calories per day to {amount}")},
                   QStringLiteral("calories")},
        IntentSpec{QStringLiteral("calories::current"),
                   QStringLiteral("when asked about the current calories"),
                   {QStringLiteral("how many calories"),
                    QStringLiteral("what is my calorie count")},
                   QStringLiteral("calories")},
    };
}

} // namespace

class TestIntent : public QObject
{
    Q_OBJECT

private slots:
    void normalizesPunctuationButKeepsNumbers();
    void parsesDigits();
    void parsesThousandsSeparators();
    void parsesNumberWords();
    void parsesCompoundNumberWords();
    void stopsAtTheFirstNonNumber();
    void hasNoQuantityWhenThereIsNoNumber();
    void findsSlotNamesInAPhrase();
    void dropsStopWordsFromADescription();
    void matchesAPhraseAndExtractsItsSlot();
    void matchesAroundPoliteness();
    void prefersTheMoreSpecificPhrase();
    void fallsBackToTheDescription();
    void refusesAnUnrelatedSentence();
    void readsYesAndNo();
    void treatsAnAmbiguousReplyAsNeither();
};

void TestIntent::normalizesPunctuationButKeepsNumbers()
{
    QCOMPARE(normalizeUtterance(QStringLiteral("Add 300 calories, please.")),
             QStringLiteral("add 300 calories please"));
    QCOMPARE(normalizeUtterance(QStringLiteral("Set it to 3.5!")),
             QStringLiteral("set it to 3.5"));
}

void TestIntent::parsesDigits()
{
    QCOMPARE(parseQuantity(QStringLiteral("300")).toDouble(), 300.0);
    QCOMPARE(parseQuantity(QStringLiteral("add 42 things")).toDouble(), 42.0);
    QCOMPARE(parseQuantity(QStringLiteral("3.5")).toDouble(), 3.5);
}

void TestIntent::parsesThousandsSeparators()
{
    QCOMPARE(parseQuantity(QStringLiteral("2,400")).toDouble(), 2400.0);
}

void TestIntent::parsesNumberWords()
{
    // Whisper writes these as often as it writes digits, which is the entire
    // reason this exists.
    QCOMPARE(parseQuantity(QStringLiteral("three hundred")).toDouble(), 300.0);
    QCOMPARE(parseQuantity(QStringLiteral("twenty")).toDouble(), 20.0);
    QCOMPARE(parseQuantity(QStringLiteral("twenty five")).toDouble(), 25.0);
}

void TestIntent::parsesCompoundNumberWords()
{
    QCOMPARE(parseQuantity(QStringLiteral("two thousand four hundred")).toDouble(), 2400.0);
    QCOMPARE(parseQuantity(QStringLiteral("one thousand")).toDouble(), 1000.0);
}

void TestIntent::stopsAtTheFirstNonNumber()
{
    QCOMPARE(parseQuantity(QStringLiteral("three hundred calories a day")).toDouble(), 300.0);
}

void TestIntent::hasNoQuantityWhenThereIsNoNumber()
{
    QVERIFY(!parseQuantity(QStringLiteral("how many calories")).isValid());
    QVERIFY(!parseQuantity(QString{}).isValid());
}

void TestIntent::findsSlotNamesInAPhrase()
{
    QCOMPARE(slotsOfPhrase(QStringLiteral("set {what} to {amount}")),
             (QStringList{QStringLiteral("what"), QStringLiteral("amount")}));
    QVERIFY(slotsOfPhrase(QStringLiteral("how many calories")).isEmpty());
}

void TestIntent::dropsStopWordsFromADescription()
{
    // "when", "asked" and "about" say nothing about which trigger was meant.
    // "adding" and "calories" do, and neither may be dropped.
    const QStringList words{contentWords(QStringLiteral("when asked about adding calories"))};
    QCOMPARE(words, (QStringList{QStringLiteral("adding"), QStringLiteral("calories")}));
}

void TestIntent::matchesAPhraseAndExtractsItsSlot()
{
    const IntentResolution match{matchIntent(QStringLiteral("add 300 calories"),
                                             calorieIntents())};
    QCOMPARE(match.id, QStringLiteral("calories::add"));
    QCOMPARE(match.captures.value(QStringLiteral("amount")).toDouble(), 300.0);
    QVERIFY(match.confidence > 0.6);
}

void TestIntent::matchesAroundPoliteness()
{
    // Nobody speaks to a robot in the words the pattern was written in.
    const IntentResolution match{
        matchIntent(QStringLiteral("hey kerfur could you add three hundred calories please"),
                    calorieIntents())};
    QCOMPARE(match.id, QStringLiteral("calories::add"));
    QCOMPARE(match.captures.value(QStringLiteral("amount")).toDouble(), 300.0);
}

void TestIntent::prefersTheMoreSpecificPhrase()
{
    // "set my daily calorie limit to 2000" contains neither of the add phrases,
    // but the point stands generally: the phrase with more literal words wins,
    // because a longer phrase can only match if more of it was actually said.
    const IntentResolution match{
        matchIntent(QStringLiteral("set my daily calorie limit to 2000"), calorieIntents())};
    QCOMPARE(match.id, QStringLiteral("calories::limit"));
    QCOMPARE(match.captures.value(QStringLiteral("amount")).toDouble(), 2000.0);
}

void TestIntent::fallsBackToTheDescription()
{
    // No phrase covers this wording. The description does: "current" and
    // "calories" are both there.
    const IntentResolution match{
        matchIntent(QStringLiteral("what are my current calories"), calorieIntents())};
    QCOMPARE(match.id, QStringLiteral("calories::current"));
}

void TestIntent::refusesAnUnrelatedSentence()
{
    const IntentResolution match{
        matchIntent(QStringLiteral("the weather is nice today"), calorieIntents())};
    QVERIFY(match.id.isEmpty());
}

void TestIntent::readsYesAndNo()
{
    QVERIFY(isAffirmative(QStringLiteral("yes")));
    QVERIFY(isAffirmative(QStringLiteral("Yeah, go ahead")));
    QVERIFY(isNegative(QStringLiteral("no")));
    QVERIFY(isNegative(QStringLiteral("cancel that")));
    QVERIFY(!isAffirmative(QStringLiteral("no")));
    QVERIFY(!isNegative(QStringLiteral("yes")));
}

void TestIntent::treatsAnAmbiguousReplyAsNeither()
{
    // "hang on" is not an answer, and reading it as one would either act or
    // cancel on something the person has not decided yet.
    QVERIFY(!isAffirmative(QStringLiteral("hang on")));
    QVERIFY(!isNegative(QStringLiteral("hang on")));
}

QTEST_MAIN(TestIntent)

#include "tst_intent.moc"
