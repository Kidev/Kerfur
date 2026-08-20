// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "transcriptfilter.h"

#include <QTest>

using namespace Kerfur;

class TestTranscriptFilter : public QObject
{
    Q_OBJECT

private slots:
    void plainSpeechSurvives()
    {
        QCOMPARE(cleanTranscript(QStringLiteral("  Hello there.  ")),
                 QStringLiteral("Hello there."));
    }

    /// Whisper describes what it hears as well as transcribing it, and a face
    /// that announces "(silence)" is worse than one that says nothing.
    void nonSpeechTagsAreNotSpeech_data()
    {
        QTest::addColumn<QString>("raw");

        QTest::newRow("blank audio") << QStringLiteral("[BLANK_AUDIO]");
        QTest::newRow("silence") << QStringLiteral(" [ Silence ] ");
        QTest::newRow("parenthesised") << QStringLiteral("(door closes)");
        QTest::newRow("music") << QStringLiteral("♪ lyrics ♪");
        QTest::newRow("asterisks") << QStringLiteral("*coughs*");
        QTest::newRow("punctuation only") << QStringLiteral(" ... ");
        QTest::newRow("empty") << QString{};
    }

    void nonSpeechTagsAreNotSpeech()
    {
        QFETCH(QString, raw);
        QVERIFY(cleanTranscript(raw).isEmpty());
    }

    void aTagInsideASentenceIsRemoved()
    {
        QCOMPARE(cleanTranscript(QStringLiteral("Hello [BLANK_AUDIO] there")),
                 QStringLiteral("Hello there"));
    }

    /// The small models loop when they cannot resolve a fragment. Forty
    /// identical words is never something a person said, and three is, so the
    /// cut has to be above the one and below the other.
    void aDecoderLoopIsCollapsed()
    {
        const QString looped{QStringLiteral("the the the the the the the the")};
        QCOMPARE(cleanTranscript(looped), QStringLiteral("the the the"));
    }

    void deliberateRepetitionIsKept()
    {
        QCOMPARE(cleanTranscript(QStringLiteral("no no no")), QStringLiteral("no no no"));
    }

    void wakeWordIsHeard_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("expected");

        QTest::newRow("plain") << QStringLiteral("hey kerfur") << true;
        QTest::newRow("capitalised") << QStringLiteral("Kerfur, look at me") << true;
        QTest::newRow("mishearing") << QStringLiteral("care fur please") << true;
        QTest::newRow("umlaut") << QStringLiteral("kerfür") << true;
        QTest::newRow("inside a longer word") << QStringLiteral("what a kerfuffle") << false;
        QTest::newRow("unrelated") << QStringLiteral("turn the lights on") << false;
    }

    void wakeWordIsHeard()
    {
        QFETCH(QString, text);
        QFETCH(bool, expected);
        QCOMPARE(containsWakeWord(text), expected);
    }
};

QTEST_MAIN(TestTranscriptFilter)
#include "tst_transcriptfilter.moc"
