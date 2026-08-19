// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "orientationresolver.h"

#include <QTest>

using namespace Kerfur;

namespace {

/// `inverted` puts the nose below the hips, which is what an upside down
/// camera produces and what the geometry check exists to notice.
PersonDetection makePerson(bool inverted)
{
    PersonDetection detection;
    detection.box = cv::Rect2f{100.0F, 100.0F, 80.0F, 200.0F};
    detection.score = 0.9F;

    const double noseY{inverted ? 260.0 : 120.0};
    const double hipY{inverted ? 130.0 : 220.0};
    detection.keypoints[static_cast<size_t>(Keypoint::Nose)] =
        Keypoint2D{QPointF{140.0, noseY}, 0.9F};
    detection.keypoints[static_cast<size_t>(Keypoint::LeftHip)] =
        Keypoint2D{QPointF{130.0, hipY}, 0.9F};
    detection.keypoints[static_cast<size_t>(Keypoint::RightHip)] =
        Keypoint2D{QPointF{150.0, hipY}, 0.9F};
    return detection;
}

} // namespace

/// Camera orientation is a runtime concern, not a mounting assumption baked
/// into a build. The camera sometimes presents upside down and the detector has
/// to recover from that on its own.
class TestOrientationResolver : public QObject
{
    Q_OBJECT

private slots:
    void staysUprightWhenPeopleAreUpright();
    void flipsAfterAConsistentRunOfInvertedPeople();
    void ignoresASingleInvertedFrame();
    void probesWhenNothingIsSeenForLongEnough();
    void manualOverrideWins();
};

void TestOrientationResolver::staysUprightWhenPeopleAreUpright()
{
    OrientationResolver resolver;
    for (int frame{0}; frame < 20; ++frame) {
        QCOMPARE(resolver.observe({makePerson(false)}, frame * 33), 0);
    }
}

void TestOrientationResolver::flipsAfterAConsistentRunOfInvertedPeople()
{
    OrientationResolver::Config config;
    config.inversionRunLength = 4;
    OrientationResolver resolver{config};

    for (int frame{0}; frame < 3; ++frame) {
        QCOMPARE(resolver.observe({makePerson(true)}, frame * 33), 0);
    }
    QCOMPARE(resolver.observe({makePerson(true)}, 99), 180);
}

void TestOrientationResolver::ignoresASingleInvertedFrame()
{
    OrientationResolver::Config config;
    config.inversionRunLength = 4;
    OrientationResolver resolver{config};

    // One frame of somebody bending over reads exactly like an upside down
    // camera, and flipping on it would make the face unusable for a second
    // every time a person picks something up.
    resolver.observe({makePerson(true)}, 0);
    resolver.observe({makePerson(true)}, 33);
    resolver.observe({makePerson(false)}, 66);
    for (int frame{3}; frame < 6; ++frame) {
        QCOMPARE(resolver.observe({makePerson(true)}, frame * 33), 0);
    }
}

void TestOrientationResolver::probesWhenNothingIsSeenForLongEnough()
{
    OrientationResolver::Config config;
    config.blindProbeIntervalMs = 1000;
    OrientationResolver resolver{config};

    // The first empty frame starts the clock rather than counting as a blind
    // interval, so a fresh start in an empty room does not flip immediately.
    QCOMPARE(resolver.observe({}, 5000), 0);
    QCOMPARE(resolver.observe({}, 5500), 0);
    QCOMPARE(resolver.observe({}, 6000), 180);
    QCOMPARE(resolver.observe({}, 7000), 0);
}

void TestOrientationResolver::manualOverrideWins()
{
    OrientationResolver::Config config;
    config.inversionRunLength = 1;
    OrientationResolver resolver{config};

    resolver.setManualOverride(90);
    QVERIFY(resolver.isManual());
    for (int frame{0}; frame < 10; ++frame) {
        QCOMPARE(resolver.observe({makePerson(true)}, frame * 33), 90);
    }

    resolver.setManualOverride(-1);
    QVERIFY(!resolver.isManual());
    QCOMPARE(resolver.observe({makePerson(true)}, 1000), 180);
}

QTEST_MAIN(TestOrientationResolver)

#include "tst_orientation.moc"
