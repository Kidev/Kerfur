// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "cameraselection.h"

#include <QTest>

using namespace Kerfur;

namespace {

CameraCandidate camera(const char *id, const char *description, bool front = false)
{
    return CameraCandidate{QByteArray{id}, QString::fromLatin1(description), front};
}

const QList<CameraCandidate> desk{
    camera("/dev/video0", "Integrated Camera"),
    camera("/dev/video2", "USB Webcam"),
    camera("/dev/video4", "Capture Card"),
};

} // namespace

/// Which camera the tracker opens, and what the panel lists for it.
class TestCameraSelection : public QObject
{
    Q_OBJECT

private slots:
    void noCameraChoosesNothing();
    void wantedCameraIsOpened();
    void absentWantedCameraFallsBack();
    void wantedCameraWinsOverTheOpenOne();
    void openCameraIsKeptWithoutAChoice();
    void frontFacingThenDefaultThenFirst();

    void labelsAreDistinct();
    void blankDescriptionsAreNumbered();
};

void TestCameraSelection::noCameraChoosesNothing()
{
    QCOMPARE(chooseCamera({}, QByteArray{"/dev/video2"}, QByteArray{}, QByteArray{}), -1);
}

void TestCameraSelection::wantedCameraIsOpened()
{
    QCOMPARE(chooseCamera(desk, "/dev/video2", {}, "/dev/video0"), 1);
}

void TestCameraSelection::absentWantedCameraFallsBack()
{
    // Unplugged: the system default is used, and nothing selects a camera that
    // is not there.
    QCOMPARE(chooseCamera(desk, "/dev/video9", {}, "/dev/video4"), 2);
}

void TestCameraSelection::wantedCameraWinsOverTheOpenOne()
{
    // A fallback is open and the chosen camera has been plugged back in.
    QCOMPARE(chooseCamera(desk, "/dev/video2", "/dev/video0", "/dev/video0"), 1);
}

void TestCameraSelection::openCameraIsKeptWithoutAChoice()
{
    // A device list refresh with no choice made leaves the open camera alone,
    // even when the default or a front facing camera would win a fresh pick.
    QList<CameraCandidate> cameras{desk};
    cameras.append(camera("/dev/video6", "Front", true));
    QCOMPARE(chooseCamera(cameras, {}, "/dev/video4", "/dev/video0"), 2);
}

void TestCameraSelection::frontFacingThenDefaultThenFirst()
{
    QList<CameraCandidate> cameras{desk};
    cameras.append(camera("/dev/video6", "Front", true));
    QCOMPARE(chooseCamera(cameras, {}, {}, "/dev/video2"), 3);

    QCOMPARE(chooseCamera(desk, {}, {}, "/dev/video2"), 1);
    QCOMPARE(chooseCamera(desk, {}, "/dev/video9", "/dev/video9"), 0);
}

void TestCameraSelection::labelsAreDistinct()
{
    const QList<CameraCandidate> twins{
        camera("a", "USB Webcam"),
        camera("b", "Integrated Camera"),
        camera("c", "USB Webcam"),
        camera("d", " USB Webcam "),
    };
    const QStringList expected{QStringLiteral("USB Webcam"),
                               QStringLiteral("Integrated Camera"),
                               QStringLiteral("USB Webcam (2)"),
                               QStringLiteral("USB Webcam (3)")};
    QCOMPARE(cameraLabels(twins), expected);
}

void TestCameraSelection::blankDescriptionsAreNumbered()
{
    const QList<CameraCandidate> cameras{camera("a", ""), camera("b", "  "), camera("c", "Desk")};
    const QStringList expected{QStringLiteral("Camera 1"),
                               QStringLiteral("Camera 2"),
                               QStringLiteral("Desk")};
    QCOMPARE(cameraLabels(cameras), expected);
}

QTEST_APPLESS_MAIN(TestCameraSelection)

#include "tst_cameraselection.moc"
