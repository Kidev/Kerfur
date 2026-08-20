// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "handlandmarker.h"

#include <QTest>

#include <cmath>

using namespace Kerfur;

namespace {

/// A hand laid out along the Y axis with the wrist at the origin: knuckles one
/// unit up, fingertips `reach` units up. Scale and position are varied by the
/// tests, because the grip measure claims to be invariant to both.
std::array<cv::Point2f, handPointCount> straightHand(float reach,
                                                     float scale = 1.0F,
                                                     cv::Point2f origin = {0.0F, 0.0F})
{
    std::array<cv::Point2f, handPointCount> hand{};
    const auto place = [&](int index, float x, float y) {
        hand[static_cast<size_t>(index)] = cv::Point2f{origin.x + x * scale, origin.y + y * scale};
    };
    place(HandWrist, 0.0F, 0.0F);
    // Four fingers, knuckle at one unit and tip at `reach` units, spread across
    // x so that they are not all the same point.
    const int knuckles[]{IndexMcp, MiddleMcp, RingMcp, PinkyMcp};
    const int tips[]{IndexTip, MiddleTip, RingTip, PinkyTip};
    for (int finger{0}; finger < 4; ++finger) {
        const auto offset{static_cast<float>(finger - 2) * 0.05F};
        place(knuckles[finger], offset, 1.0F);
        place(tips[finger], offset, reach);
    }
    return hand;
}

/// Which fingers are out, for a hand the tests are about to build.
struct HandSpec
{
    bool index{false};
    bool middle{false};
    bool ring{false};
    bool pinky{false};
    bool thumbOut{false};
};

/// A hand whose fingers point along `up`, built from that direction and the one
/// across the palm rather than from absolute coordinates.
///
/// That is the whole point of the helper: the same specification turned through
/// any angle is the same hand, so a test can ask what a shape means when the
/// hand is upright, on its side, or upside down without writing three tables of
/// numbers and getting one of them subtly wrong.
std::array<cv::Point2f, handPointCount> posedHand(cv::Point2f up,
                                                  HandSpec spec,
                                                  float scale = 100.0F,
                                                  cv::Point2f origin = {0.0F, 0.0F})
{
    const float length{std::hypot(up.x, up.y)};
    const cv::Point2f along{up.x / length, up.y / length};
    const cv::Point2f across{-along.y, along.x};

    std::array<cv::Point2f, handPointCount> hand{};
    const auto place = [&](int index, float alongBy, float acrossBy) {
        hand[static_cast<size_t>(index)] =
            cv::Point2f{origin.x + (along.x * alongBy + across.x * acrossBy) * scale,
                        origin.y + (along.y * alongBy + across.y * acrossBy) * scale};
    };

    // Measured hands put an extended tip about 1.75 knuckle distances from the
    // wrist and a curled one about 0.78, which are the two bands gripStrength
    // was calibrated against.
    constexpr float outReach{1.75F};
    constexpr float inReach{0.78F};
    const int knuckles[]{IndexMcp, MiddleMcp, RingMcp, PinkyMcp};
    const int tips[]{IndexTip, MiddleTip, RingTip, PinkyTip};
    const bool out[]{spec.index, spec.middle, spec.ring, spec.pinky};
    for (int finger{0}; finger < 4; ++finger) {
        const auto offset{static_cast<float>(finger - 2) * 0.2F + 0.1F};
        const float reach{out[finger] ? outReach : inReach};
        place(knuckles[finger], 1.0F, offset);
        place(tips[finger], reach, offset * reach);
    }

    place(HandWrist, 0.0F, 0.0F);
    place(ThumbCmc, 0.2F, -0.3F);
    place(ThumbMcp, 0.4F, -0.6F);
    // Out to the side of the palm, or folded back across it.
    place(ThumbIp, spec.thumbOut ? 0.5F : 0.55F, spec.thumbOut ? -0.9F : -0.3F);
    place(ThumbTip, spec.thumbOut ? 0.6F : 0.7F, spec.thumbOut ? -1.1F : 0.0F);
    return hand;
}

} // namespace

class TestHandLandmarker : public QObject
{
    Q_OBJECT

private slots:
    void anchorsMatchThePublishedTable();
    void anchorsAreTheStandardGrid();
    void gripIsZeroForAnOpenHand();
    void gripSaturatesForAFist();
    void gripIsInvariantToScaleAndPosition();
    void gripIsMonotonic();
    void gripIgnoresAFoldedThumb();
    void gripSurvivesADegenerateHand();
    void decodeRejectsMalformedOutputs();
    void readsAnOpenPalmAndAFist();
    void readsAThumbUpWhicheverWayTheHandIsTurned();
    void readsAThumbDown();
    void refusesAThumbHeldSideways();
    void readsAPointAndAVictory();
    void readsTheRing();
    void refusesAHandCaughtBetweenShapes();
    void gestureSurvivesScaleAndPosition();
    void namesEveryShapeItReports();
};

/// The first and last rows of upstream's 2016 entry literal table. The
/// generator has to agree with the file the model was exported against, and
/// these two rows pin both ends of it.
void TestHandLandmarker::anchorsMatchThePublishedTable()
{
    const std::vector<cv::Point2f> &anchors{palmAnchors()};
    QCOMPARE(anchors.size(), size_t{2016});

    QVERIFY(std::abs(anchors.front().x - 0.02083333F) < 1e-6F);
    QVERIFY(std::abs(anchors.front().y - 0.02083333F) < 1e-6F);
    // Two anchors per cell in the first layer, so the second entry repeats.
    QCOMPARE(anchors[0], anchors[1]);
    QVERIFY(std::abs(anchors[2].x - 0.0625F) < 1e-6F);

    // Last cell of the 12x12 layer, six deep.
    QVERIFY(std::abs(anchors.back().x - (11.5F / 12.0F)) < 1e-6F);
    QVERIFY(std::abs(anchors.back().y - (11.5F / 12.0F)) < 1e-6F);
    QCOMPARE(anchors[2015], anchors[2010]);
}

void TestHandLandmarker::anchorsAreTheStandardGrid()
{
    const std::vector<cv::Point2f> &anchors{palmAnchors()};
    // 24x24 with two per cell, then 12x12 with six.
    QCOMPARE(anchors.size(), size_t{24 * 24 * 2 + 12 * 12 * 6});
    for (const cv::Point2f &anchor : anchors) {
        QVERIFY(anchor.x > 0.0F && anchor.x < 1.0F);
        QVERIFY(anchor.y > 0.0F && anchor.y < 1.0F);
    }
}

void TestHandLandmarker::gripIsZeroForAnOpenHand()
{
    // Tips well beyond the knuckles, which is what an extended finger looks
    // like: measured open hands sit between 1.53 and 1.86.
    QCOMPARE(gripStrength(straightHand(1.8F)), 0.0);
}

void TestHandLandmarker::gripSaturatesForAFist()
{
    // Measured fists sit between 0.74 and 0.80.
    QCOMPARE(gripStrength(straightHand(0.78F)), 1.0);
}

void TestHandLandmarker::gripIsInvariantToScaleAndPosition()
{
    const double near{gripStrength(straightHand(1.2F, 220.0F, cv::Point2f{640.0F, 480.0F}))};
    const double far{gripStrength(straightHand(1.2F, 18.0F, cv::Point2f{-5.0F, 3.0F}))};
    // A hand across the room and a hand at the lens are the same hand. Without
    // the ratio this would be the test that fails.
    QVERIFY(std::abs(near - far) < 1e-6);
    QVERIFY(near > 0.0 && near < 1.0);
}

void TestHandLandmarker::gripIsMonotonic()
{
    double previous{-1.0};
    // Curling the fingers in must never make the hand read as more open.
    for (const float reach : {1.9F, 1.7F, 1.5F, 1.3F, 1.1F, 0.9F, 0.7F}) {
        const double grip{gripStrength(straightHand(reach))};
        QVERIFY2(grip >= previous, qPrintable(QStringLiteral("reach %1").arg(static_cast<double>(reach))));
        previous = grip;
    }
    QCOMPARE(previous, 1.0);
}

void TestHandLandmarker::gripIgnoresAFoldedThumb()
{
    // A thumbs up is four curled fingers and one extended thumb, and it must
    // read as closed. The thumb is deliberately not part of the measure, so
    // moving it anywhere at all must change nothing.
    auto hand{straightHand(0.78F)};
    const double before{gripStrength(hand)};
    hand[ThumbTip] = cv::Point2f{-40.0F, -40.0F};
    hand[ThumbIp] = cv::Point2f{-30.0F, -20.0F};
    hand[ThumbMcp] = cv::Point2f{-10.0F, -5.0F};
    QCOMPARE(gripStrength(hand), before);
    QCOMPARE(before, 1.0);
}

void TestHandLandmarker::gripSurvivesADegenerateHand()
{
    // Every point on top of the wrist: no knuckle distance to divide by. It
    // must answer, not divide by zero.
    std::array<cv::Point2f, handPointCount> collapsed{};
    const double grip{gripStrength(collapsed)};
    QVERIFY(grip >= 0.0 && grip <= 1.0);
}

void TestHandLandmarker::decodeRejectsMalformedOutputs()
{
    // No tensors at all, and tensors of the wrong rank: the decode is fed
    // whatever the ONNX runtime produced and must not index into it blindly.
    QVERIFY(decodePalms({}, cv::Size{640, 480}, cv::Point2f{}, 0.5F, 0.3F).isEmpty());

    const int sizes[3]{1, 2016, 7};
    const cv::Mat wrongTrailing{3, sizes, CV_32F, cv::Scalar{0.0}};
    QVERIFY(decodePalms({wrongTrailing}, cv::Size{640, 480}, cv::Point2f{}, 0.5F, 0.3F).isEmpty());

    // Well formed but every score at zero, which sigmoids to 0.5 and so must
    // fall under a 0.9 threshold rather than producing 2016 palms.
    const int boxSizes[3]{1, 2016, 18};
    const int scoreSizes[3]{1, 2016, 1};
    const cv::Mat boxes{3, boxSizes, CV_32F, cv::Scalar{0.0}};
    const cv::Mat scores{3, scoreSizes, CV_32F, cv::Scalar{0.0}};
    QVERIFY(decodePalms({boxes, scores}, cv::Size{640, 480}, cv::Point2f{}, 0.9F, 0.3F).isEmpty());
}

void TestHandLandmarker::readsAnOpenPalmAndAFist()
{
    const cv::Point2f up{0.0F, -1.0F};
    QCOMPARE(handGesture(posedHand(up, HandSpec{true, true, true, true, true})),
             HandGesture::OpenPalm);
    // A flat hand with the thumb tucked in is still a flat hand.
    QCOMPARE(handGesture(posedHand(up, HandSpec{true, true, true, true, false})),
             HandGesture::OpenPalm);
    QCOMPARE(handGesture(posedHand(up, HandSpec{})), HandGesture::Fist);
}

/// The gesture that has to survive the hand being sideways, because that is how
/// it is actually made: the fingers curl across the frame and the thumb, not
/// the hand, is what points up.
void TestHandLandmarker::readsAThumbUpWhicheverWayTheHandIsTurned()
{
    const HandSpec fistWithThumb{false, false, false, false, true};
    // Fingers to the right, thumb up.
    QCOMPARE(handGesture(posedHand(cv::Point2f{1.0F, 0.0F}, fistWithThumb)), HandGesture::ThumbUp);
    // The same hand rolled a little further, as a real one never sits square.
    QCOMPARE(handGesture(posedHand(cv::Point2f{1.0F, -0.4F}, fistWithThumb)), HandGesture::ThumbUp);
}

void TestHandLandmarker::readsAThumbDown()
{
    const HandSpec fistWithThumb{false, false, false, false, true};
    QCOMPARE(handGesture(posedHand(cv::Point2f{-1.0F, 0.0F}, fistWithThumb)),
             HandGesture::ThumbDown);
}

/// A thumb held out to the side is neither approval nor its opposite, and
/// reporting one of the two because it is nearer would be a coin toss made at
/// the moment somebody is waiting to see whether Kerfur agreed.
void TestHandLandmarker::refusesAThumbHeldSideways()
{
    const HandSpec fistWithThumb{false, false, false, false, true};
    QCOMPARE(handGesture(posedHand(cv::Point2f{0.0F, -1.0F}, fistWithThumb)), HandGesture::Fist);
}

void TestHandLandmarker::readsAPointAndAVictory()
{
    const cv::Point2f up{0.0F, -1.0F};
    QCOMPARE(handGesture(posedHand(up, HandSpec{true, false, false, false, false})),
             HandGesture::Point);
    QCOMPARE(handGesture(posedHand(up, HandSpec{true, true, false, false, false})),
             HandGesture::Victory);
    // Index and pinky out is a different shape entirely, and an unnamed one.
    QCOMPARE(handGesture(posedHand(up, HandSpec{true, false, false, true, false})),
             HandGesture::None);
}

/// The one shape whose index finger is neither out nor curled, but drawn round
/// to meet the thumb. It is recognised by the two tips touching while the other
/// three fingers stand clear, which is what a person looking at it sees.
void TestHandLandmarker::readsTheRing()
{
    std::array<cv::Point2f, handPointCount> hand{
        posedHand(cv::Point2f{0.0F, -1.0F}, HandSpec{false, true, true, true, true})};
    hand[IndexTip] = hand[ThumbTip] + cv::Point2f{4.0F, 4.0F};
    QCOMPARE(handGesture(hand), HandGesture::Ok);
}

/// Halfway between two shapes is not a third shape. The gesture is about to be
/// acted on, and a wrong answer costs more than no answer.
void TestHandLandmarker::refusesAHandCaughtBetweenShapes()
{
    std::array<cv::Point2f, handPointCount> hand{
        posedHand(cv::Point2f{0.0F, -1.0F}, HandSpec{true, true, true, true, true})};
    // The ring finger halfway in, between the two bands.
    hand[RingTip] = hand[RingMcp] * 1.25F;
    QCOMPARE(handGesture(hand), HandGesture::None);
}

void TestHandLandmarker::gestureSurvivesScaleAndPosition()
{
    const HandSpec victory{true, true, false, false, false};
    const cv::Point2f up{0.0F, -1.0F};
    QCOMPARE(handGesture(posedHand(up, victory, 12.0F, cv::Point2f{5.0F, 900.0F})),
             HandGesture::Victory);
    QCOMPARE(handGesture(posedHand(up, victory, 400.0F, cv::Point2f{-70.0F, 30.0F})),
             HandGesture::Victory);
}

/// Every reported shape has to have a name, because the name is what reaches
/// the panel and, eventually, a skill. Only `None` is allowed to be empty.
void TestHandLandmarker::namesEveryShapeItReports()
{
    const HandGesture shapes[]{HandGesture::OpenPalm,
                               HandGesture::Fist,
                               HandGesture::ThumbUp,
                               HandGesture::ThumbDown,
                               HandGesture::Point,
                               HandGesture::Victory,
                               HandGesture::Ok};
    for (const HandGesture shape : shapes) {
        QVERIFY2(!gestureName(shape).isEmpty(),
                 qPrintable(QString::number(static_cast<int>(shape))));
    }
    QVERIFY(gestureName(HandGesture::None).isEmpty());
}

QTEST_APPLESS_MAIN(TestHandLandmarker)

#include "tst_handlandmarker.moc"
