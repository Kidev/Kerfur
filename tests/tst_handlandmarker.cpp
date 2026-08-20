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

QTEST_APPLESS_MAIN(TestHandLandmarker)

#include "tst_handlandmarker.moc"
