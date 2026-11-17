// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "session.h"

#include <QTest>

using namespace Kerfur;

/// The command line that decides whether a run loads its models. A false
/// positive leaves Kerfur blind and deaf with nothing to say why; a false
/// negative downloads and loads every model the light page promised to skip.
class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void optionStartsALightSession();
    void plainCommandLineIsAFullSession();
    void programNameIsNotAnOption();
    void lookalikeOptionsAreIgnored();
    void thisSuiteRunsAFullSession();
};

void TestSession::optionStartsALightSession()
{
    QVERIFY(Session::lightIn({QStringLiteral("kerfur"), QStringLiteral("--light")}));
    QVERIFY(Session::lightIn(
        {QStringLiteral("./this.program"), QStringLiteral("-x"), QStringLiteral("--light")}));
}

void TestSession::plainCommandLineIsAFullSession()
{
    QVERIFY(!Session::lightIn({}));
    QVERIFY(!Session::lightIn({QStringLiteral("kerfur")}));
}

void TestSession::programNameIsNotAnOption()
{
    QVERIFY(!Session::lightIn({QStringLiteral("--light")}));
}

void TestSession::lookalikeOptionsAreIgnored()
{
    QVERIFY(!Session::lightIn({QStringLiteral("kerfur"), QStringLiteral("light")}));
    QVERIFY(!Session::lightIn({QStringLiteral("kerfur"), QStringLiteral("--lightweight")}));
    QVERIFY(!Session::lightIn({QStringLiteral("kerfur"), QStringLiteral("--light=0")}));
}

void TestSession::thisSuiteRunsAFullSession()
{
    QVERIFY(!Session::light());
    QVERIFY(!Session{}.isLight());
}

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
