// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "faceanalysisworker.h"

#include <QTest>

using namespace Kerfur;

namespace {

constexpr float confident{0.9F};
constexpr float floorConfidence{0.4F};
const QSize frame{1000, 1000};

/// A person with every keypoint at the origin and nothing believed, so that a
/// test only has to place the two points it cares about.
AnalysedPerson emptyPerson()
{
    AnalysedPerson person;
    person.box = QRectF{0.2, 0.1, 0.6, 0.8};
    for (int index{0}; index < keypointCount; ++index) {
        person.keypoints.append(QPointF{0.0, 0.0});
        person.keypointScores.append(0.0);
    }
    return person;
}

void place(AnalysedPerson &person, Keypoint keypoint, QPointF at, double score)
{
    const int index{static_cast<int>(keypoint)};
    person.keypoints[index] = at;
    person.keypointScores[index] = score;
}

} // namespace

class TestHandGeometry : public QObject
{
    Q_OBJECT

private slots:
    void refusesAWristItCannotSee();
    void placesTheHandBeyondTheWrist();
    void scalesTheHandWithTheForearm();
    void isSquareInPixelsNotInNormalizedUnits();
    void fallsBackToThePersonWhenTheElbowIsHidden();
    void handlesAnArmPointingAtTheCamera();
    void reportsTheSideItWasAskedFor();
    void measuresOverlapAgainstTheHand();
    void ignoresAnObjectThatMerelyPassesNearby();
};

void TestHandGeometry::refusesAWristItCannotSee()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.5}, 0.2);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(!hand.valid);
    QVERIFY(hand.box.isNull());
}

void TestHandGeometry::placesTheHandBeyondTheWrist()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftElbow, QPointF{0.4, 0.5}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    // Along the forearm, past the wrist, never behind it.
    QVERIFY(hand.box.center().x() > 0.5);
    QCOMPARE(hand.wrist, QPointF(0.5, 0.5));
    QVERIFY(qFuzzyCompare(hand.box.center().y() + 1.0, 0.5 + 1.0));
}

void TestHandGeometry::scalesTheHandWithTheForearm()
{
    AnalysedPerson near{emptyPerson()};
    place(near, Keypoint::LeftElbow, QPointF{0.2, 0.5}, confident);
    place(near, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    AnalysedPerson far{emptyPerson()};
    place(far, Keypoint::LeftElbow, QPointF{0.45, 0.5}, confident);
    place(far, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    const auto measure = [](const AnalysedPerson &person) {
        return handFromKeypoints(person,
                                 Keypoint::LeftWrist,
                                 Keypoint::LeftElbow,
                                 QStringLiteral("left"),
                                 frame,
                                 floorConfidence)
            .box.width();
    };
    // A hand across the room is smaller than one at the desk, which is what
    // makes one constant work at both distances.
    QVERIFY(measure(near) > measure(far) * 3.0);
}

void TestHandGeometry::isSquareInPixelsNotInNormalizedUnits()
{
    // The frame is 16:9, so a box that is square in normalized units is not
    // square on screen. Lengths are measured in pixels for this reason.
    const QSize wide{1920, 1080};
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftElbow, QPointF{0.4, 0.5}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              wide,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    const double pixelWidth{hand.box.width() * wide.width()};
    const double pixelHeight{hand.box.height() * wide.height()};
    QVERIFY(std::abs(pixelWidth - pixelHeight) < 1.0);
}

void TestHandGeometry::fallsBackToThePersonWhenTheElbowIsHidden()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::RightWrist, QPointF{0.5, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::RightWrist,
                                              Keypoint::RightElbow,
                                              QStringLiteral("right"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    QVERIFY(hand.box.width() > 0.0);
    // Centred on the wrist, because without an elbow there is no direction to
    // reach in.
    QVERIFY(qFuzzyCompare(hand.box.center().x() + 1.0, 0.5 + 1.0));
}

void TestHandGeometry::handlesAnArmPointingAtTheCamera()
{
    // Elbow and wrist project onto the same point. The forearm has no length,
    // so the person's own height is the only scale left.
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftElbow, QPointF{0.5, 0.5}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    QVERIFY(hand.box.width() > 0.0);
}

void TestHandGeometry::reportsTheSideItWasAskedFor()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::RightWrist, QPointF{0.5, 0.5}, confident);

    QCOMPARE(handFromKeypoints(person,
                               Keypoint::RightWrist,
                               Keypoint::RightElbow,
                               QStringLiteral("right"),
                               frame,
                               floorConfidence)
                 .side,
             QStringLiteral("right"));
}

void TestHandGeometry::measuresOverlapAgainstTheHand()
{
    const QRectF hand{0.50, 0.50, 0.04, 0.04};
    // A guitar: far larger than the hand and completely containing it. Against
    // the union this would score near zero and the hold would be missed.
    const QRectF guitar{0.20, 0.20, 0.60, 0.60};
    QCOMPARE(handInsideObject(hand, guitar), 1.0);
}

void TestHandGeometry::ignoresAnObjectThatMerelyPassesNearby()
{
    const QRectF hand{0.50, 0.50, 0.04, 0.04};
    const QRectF elsewhere{0.10, 0.10, 0.20, 0.20};
    QCOMPARE(handInsideObject(hand, elsewhere), 0.0);
}

QTEST_MAIN(TestHandGeometry)

#include "tst_handgeometry.moc"
