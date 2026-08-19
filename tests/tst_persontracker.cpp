// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "persontracker.h"

#include <QTest>

using namespace Kerfur;

namespace {

PersonDetection makeDetection(float x, float y, float width, float height, float noseOffsetX)
{
    PersonDetection detection;
    detection.box = cv::Rect2f{x, y, width, height};
    detection.score = 0.9F;

    // A skeleton that differs between people, so that the keypoint term in the
    // association cost has something to say when two boxes overlap.
    detection.keypoints[static_cast<size_t>(Keypoint::Nose)] =
        Keypoint2D{QPointF{x + noseOffsetX, y + 10.0}, 0.9F};
    detection.keypoints[static_cast<size_t>(Keypoint::LeftHip)] =
        Keypoint2D{QPointF{x + noseOffsetX, y + (height * 0.6)}, 0.9F};
    detection.keypoints[static_cast<size_t>(Keypoint::RightHip)] =
        Keypoint2D{QPointF{x + noseOffsetX + 10.0, y + (height * 0.6)}, 0.9F};
    return detection;
}

} // namespace

/// Tracking is what turns per frame detections into people who persist.
///
/// Without it the person count flickers, the identity vote has nothing to
/// accumulate across, and every consumer downstream sees somebody arriving and
/// leaving several times a second.
class TestPersonTracker : public QObject
{
    Q_OBJECT

private slots:
    void confirmsOnlyAfterRepeatedSightings();
    void keepsIdentityAcrossFrames();
    void forgetsAfterEnoughMisses();
    void identityVoteNeedsAMajority();
    void identityVoteHoldsThroughOneDisagreement();
};

void TestPersonTracker::confirmsOnlyAfterRepeatedSightings()
{
    PersonTracker tracker;
    const PersonDetection person{makeDetection(100.0F, 100.0F, 80.0F, 200.0F, 40.0F)};

    tracker.update({person}, 0);
    QVERIFY(tracker.confirmedTracks().isEmpty());

    tracker.update({person}, 33);
    QVERIFY(tracker.confirmedTracks().isEmpty());

    // Three hits is the default. A single frame detection on a patterned
    // background is common; three consecutive ones in the same place are not.
    tracker.update({person}, 66);
    QCOMPARE(tracker.confirmedTracks().size(), 1);
}

void TestPersonTracker::keepsIdentityAcrossFrames()
{
    PersonTracker tracker;

    int firstId{-1};
    for (int frame{0}; frame < 6; ++frame) {
        const float drift{static_cast<float>(frame) * 4.0F};
        tracker.update({makeDetection(100.0F + drift, 100.0F, 80.0F, 200.0F, 40.0F)}, frame * 33);
        if (frame == 2) {
            firstId = tracker.confirmedTracks().first()->id;
        }
    }

    QCOMPARE(tracker.confirmedTracks().size(), 1);
    QCOMPARE(tracker.confirmedTracks().first()->id, firstId);
}

void TestPersonTracker::forgetsAfterEnoughMisses()
{
    PersonTracker::Config config;
    config.minimumHits = 1;
    config.maximumMisses = 3;
    PersonTracker tracker{config};

    tracker.update({makeDetection(100.0F, 100.0F, 80.0F, 200.0F, 40.0F)}, 0);
    QCOMPARE(tracker.confirmedTracks().size(), 1);

    for (int frame{1}; frame <= 3; ++frame) {
        tracker.update({}, frame * 33);
        QCOMPARE(tracker.tracks().size(), size_t{1});
    }

    // A track survives a person turning side on, and does not survive them
    // leaving the room.
    tracker.update({}, 132);
    QVERIFY(tracker.tracks().empty());
}

void TestPersonTracker::identityVoteNeedsAMajority()
{
    IdentityVote vote;

    for (int observation{0}; observation < 3; ++observation) {
        vote.observe(QStringLiteral("kidev"), 0.5);
        QVERIFY(vote.settled().isEmpty());
    }

    vote.observe(QStringLiteral("kidev"), 0.5);
    QCOMPARE(vote.settled(), QStringLiteral("kidev"));
    QCOMPARE(vote.confidence(), 0.5);
}

void TestPersonTracker::identityVoteHoldsThroughOneDisagreement()
{
    IdentityVote vote;
    for (int observation{0}; observation < 4; ++observation) {
        vote.observe(QStringLiteral("kidev"), 0.5);
    }
    QCOMPARE(vote.settled(), QStringLiteral("kidev"));

    // One bad embedding out of a run of good ones is the normal case, not a
    // different person. Reacting to it is what makes the identity sensor
    // oscillate.
    vote.observe(QString{}, 0.0);
    QCOMPARE(vote.settled(), QStringLiteral("kidev"));

    for (int observation{0}; observation < 7; ++observation) {
        vote.observe(QString{}, 0.0);
    }
    QVERIFY(vote.settled().isEmpty());
}

QTEST_MAIN(TestPersonTracker)

#include "tst_persontracker.moc"
