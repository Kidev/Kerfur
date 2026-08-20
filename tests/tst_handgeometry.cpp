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
    void readsTheHandScaleOffTheLargestVisibleCue();
    void growsAForeshortenedForearmBackToBodyScale();
    void leavesAPlausibleForearmAlone();
    void refusesAForearmThatWouldMakeAHandBiggerThanTheBody();
    void cropsForObjectsAroundTheHand();
    void keepsAnEdgeCropWholeAndInsideTheFrame();
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

void TestHandGeometry::readsTheHandScaleOffTheLargestVisibleCue()
{
    AnalysedPerson person{emptyPerson()};
    // Shoulders three tenths of the frame apart: half of that is a hand, so
    // 150 pixels. Eyes six hundredths apart: three times that is 180. The
    // larger wins, because a shortened length is a body turned away and a
    // lengthened one does not happen.
    place(person, Keypoint::LeftShoulder, QPointF{0.35, 0.5}, confident);
    place(person, Keypoint::RightShoulder, QPointF{0.65, 0.5}, confident);
    place(person, Keypoint::LeftEye, QPointF{0.47, 0.4}, confident);
    place(person, Keypoint::RightEye, QPointF{0.53, 0.4}, confident);

    QCOMPARE(bodyHandScale(person, frame, floorConfidence), 180.0);
}

void TestHandGeometry::growsAForeshortenedForearmBackToBodyScale()
{
    // An arm reaching towards the camera. The forearm projects to ten pixels,
    // which would size the hand at eight and hand the palm detector a crop with
    // no palm in it -- and this is the nearest hand to the lens, so it is both
    // the largest hand in the frame and the one most likely to be holding
    // something.
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftShoulder, QPointF{0.35, 0.5}, confident);
    place(person, Keypoint::RightShoulder, QPointF{0.65, 0.5}, confident);
    place(person, Keypoint::LeftElbow, QPointF{0.5, 0.5}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.51, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    // Two thirds of what the shoulders say a hand is, and nowhere near what the
    // forearm alone said.
    QCOMPARE(hand.box.width() * frame.width(), 97.5);
}

void TestHandGeometry::leavesAPlausibleForearmAlone()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftShoulder, QPointF{0.35, 0.5}, confident);
    place(person, Keypoint::RightShoulder, QPointF{0.65, 0.5}, confident);
    place(person, Keypoint::LeftElbow, QPointF{0.3, 0.5}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.5}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    // The forearm's own answer, untouched: the body agrees it is possible.
    QCOMPARE(hand.box.width() * frame.width(), 160.0);
}

void TestHandGeometry::refusesAForearmThatWouldMakeAHandBiggerThanTheBody()
{
    AnalysedPerson person{emptyPerson()};
    place(person, Keypoint::LeftShoulder, QPointF{0.35, 0.5}, confident);
    place(person, Keypoint::RightShoulder, QPointF{0.65, 0.5}, confident);
    place(person, Keypoint::LeftElbow, QPointF{0.5, 0.1}, confident);
    place(person, Keypoint::LeftWrist, QPointF{0.5, 0.9}, confident);

    const AnalysedHand hand{handFromKeypoints(person,
                                              Keypoint::LeftWrist,
                                              Keypoint::LeftElbow,
                                              QStringLiteral("left"),
                                              frame,
                                              floorConfidence)};
    QVERIFY(hand.valid);
    QCOMPARE(hand.box.width() * frame.width(), 270.0);
}

void TestHandGeometry::cropsForObjectsAroundTheHand()
{
    AnalysedHand hand;
    hand.valid = true;
    hand.box = QRectF{0.45, 0.45, 0.1, 0.1};

    const cv::Rect region{objectRegionForHand(hand, cv::Size{1000, 1000})};
    // Square, centred on the hand, and several hands across: the detector has
    // one fixed input side and this is the only way a held object arrives at it
    // large enough to be named.
    QCOMPARE(region.width, region.height);
    QCOMPARE(region.width, 350);
    QCOMPARE(region.x + region.width / 2, 500);
    QCOMPARE(region.y + region.height / 2, 500);
}

void TestHandGeometry::keepsAnEdgeCropWholeAndInsideTheFrame()
{
    AnalysedHand hand;
    hand.valid = true;
    hand.box = QRectF{0.0, 0.0, 0.04, 0.04};

    const cv::Rect region{objectRegionForHand(hand, cv::Size{1000, 1000})};
    // Slid back inside the frame rather than cut off at it. An arm comes into
    // shot before its owner does, and a crop that loses half of itself there
    // hands the network a sliver of what it was trained on.
    QCOMPARE(region.width, 140);
    QCOMPARE(region.height, 140);
    QCOMPARE(region.x, 0);
    QCOMPARE(region.y, 0);
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
