// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FaceTracker.hpp"

#include <QCoreApplication>
#include <QTest>

/// The camera and the vision models are two separate things.
///
/// A build without models still shows the camera. This case runs in a process
/// that has no model directory, which is the state of a fresh install, and pins
/// that the worker's report lands on `perceptionProblem` and leaves the camera
/// error string alone.
class TestVisionCapture : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void missingModelsAreNotACameraError();
};

void TestVisionCapture::initTestCase()
{
    // Named so that the model search finds nothing provisioned for another
    // Kerfur build in the application data location.
    QCoreApplication::setOrganizationName(QStringLiteral("kidev"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_visioncapture"));
}

void TestVisionCapture::missingModelsAreNotACameraError()
{
    const FaceTracker tracker;

    // The worker loads on its own thread and reports once it has looked.
    QTRY_VERIFY_WITH_TIMEOUT(!tracker.perceptionProblem().isEmpty(), 10000);
    QVERIFY(tracker.errorString().isEmpty());
}

QTEST_MAIN(TestVisionCapture)

#include "tst_visioncapture.moc"
