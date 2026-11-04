// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "capturepermission.h"

#include <QTest>

using namespace Kerfur;

using Step = CapturePermission::Step;

/// The gate in front of every camera and microphone start.
///
/// Only a refusal may keep capture shut, and a request the platform could not
/// settle must not be asked again forever, or the browser build would never
/// open a device that turns up later.
class TestCapturePermission : public QObject
{
    Q_OBJECT

private slots:
    void grantedProceeds();
    void deniedRefuses();
    void deniedRefusesEvenAfterAsking();
    void undeterminedAsksOnce();
    void undeterminedWaitsWhileAsking();
    void undeterminedProceedsOnceAnswered();
    void grantedProceedsWhileAnotherRequestIsInFlight();
};

void TestCapturePermission::grantedProceeds()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Granted, false, false),
             Step::Proceed);
}

void TestCapturePermission::deniedRefuses()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Denied, false, false),
             Step::Refuse);
}

void TestCapturePermission::deniedRefusesEvenAfterAsking()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Denied, true, false),
             Step::Refuse);
}

void TestCapturePermission::undeterminedAsksOnce()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Undetermined, false, false),
             Step::Request);
}

void TestCapturePermission::undeterminedWaitsWhileAsking()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Undetermined, false, true),
             Step::Wait);
}

void TestCapturePermission::undeterminedProceedsOnceAnswered()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Undetermined, true, false),
             Step::Proceed);
}

void TestCapturePermission::grantedProceedsWhileAnotherRequestIsInFlight()
{
    QCOMPARE(CapturePermission::nextStep(Qt::PermissionStatus::Granted, false, true),
             Step::Proceed);
}

QTEST_GUILESS_MAIN(TestCapturePermission)

#include "tst_capturepermission.moc"
