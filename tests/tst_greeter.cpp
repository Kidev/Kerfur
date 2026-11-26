// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "greeter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Kerfur;

namespace {

constexpr qint64 hour{3'600'000};
const QString alice{QStringLiteral("alice")};

void storeAbsence(qint64 absence, qint64 savedAgo)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Greetings/absences"),
                      QVariantMap{{alice, QVariant::fromValue<qlonglong>(absence)}});
    settings.setValue(QStringLiteral("Greetings/savedAt"),
                      QDateTime::currentMSecsSinceEpoch() - savedAgo);
}

} // namespace

/// Who Kerfur greets when they come into view, and what it remembers of them
/// from one run to the next.
class TestGreeter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void aFirstSightingIsGreeted();
    void somebodyInViewIsGreetedOnce();
    void aNameListedTwiceIsGreetedOnce();
    void disabledCountsWithoutGreeting();
    void aRecentSightingSurvivesARestart();
    void aLongAbsenceSurvivesARestart();
    void timeClosedCountsAsAbsence();
    void timeClosedBelowTheThresholdIsNotAReunion();

private:
    QTemporaryDir m_settingsDirectory;
};

void TestGreeter::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("kerfur-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_greeter"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void TestGreeter::init()
{
    QSettings{}.clear();
}

void TestGreeter::aFirstSightingIsGreeted()
{
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice});
    QCOMPARE(greeted.size(), 1);
    QCOMPARE(greeted.first().first().toString(), alice);
}

void TestGreeter::somebodyInViewIsGreetedOnce()
{
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice});
    greeter.see({alice});
    greeter.see({alice});
    QCOMPARE(greeted.size(), 1);
}

void TestGreeter::aNameListedTwiceIsGreetedOnce()
{
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice, alice, QString{}});
    QCOMPARE(greeted.size(), 1);
}

/// Somebody in view while greetings are off is not greeted the moment they
/// are turned on.
void TestGreeter::disabledCountsWithoutGreeting()
{
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.setEnabled(false);
    greeter.see({alice});
    QCOMPARE(greeted.size(), 0);
    greeter.setEnabled(true);
    greeter.see({alice});
    QCOMPARE(greeted.size(), 0);
}

void TestGreeter::aRecentSightingSurvivesARestart()
{
    {
        Greeter before;
        before.see({alice});
    }
    Greeter after;
    QSignalSpy greeted{&after, &Greeter::greeted};
    after.see({alice});
    QCOMPARE(greeted.size(), 0);
}

void TestGreeter::aLongAbsenceSurvivesARestart()
{
    storeAbsence(9 * hour, 0);
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice});
    QCOMPARE(greeted.size(), 1);
}

void TestGreeter::timeClosedCountsAsAbsence()
{
    storeAbsence(1 * hour, 7 * hour);
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice});
    QCOMPARE(greeted.size(), 1);
}

void TestGreeter::timeClosedBelowTheThresholdIsNotAReunion()
{
    storeAbsence(1 * hour, 6 * hour);
    Greeter greeter;
    QSignalSpy greeted{&greeter, &Greeter::greeted};
    greeter.see({alice});
    QCOMPARE(greeted.size(), 0);
}

QTEST_GUILESS_MAIN(TestGreeter)

#include "tst_greeter.moc"
