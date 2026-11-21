// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "acceleratordecode.h"

#include <QTest>

#include <cmath>
#include <string>
#include <vector>

using namespace Kerfur;

namespace {

/// A 64 pixel input with a 2x2 grid: a stride of 32, small enough to work out
/// every expected value by hand.
constexpr int inputSide{64};
constexpr int grid{2};
constexpr float stride{32.0F};

Accel::Tensor gridTensor(const std::string &name, int channels, float fill = 0.0F)
{
    Accel::Tensor tensor;
    tensor.info.name = name;
    tensor.info.height = grid;
    tensor.info.width = grid;
    tensor.info.channels = channels;
    tensor.values.assign(tensor.info.elements(), fill);
    return tensor;
}

float &at(Accel::Tensor &tensor, int row, int column, int channel)
{
    return tensor.values[(static_cast<std::size_t>(row) * grid + static_cast<std::size_t>(column)) *
                             static_cast<std::size_t>(tensor.info.channels) +
                         static_cast<std::size_t>(channel)];
}

/// A distribution over sixteen bins with all its weight on `bin`, so that
/// its expectation is `bin`.
void setDistance(Accel::Tensor &box, int row, int column, int side, int bin)
{
    for (int index{0}; index < 16; ++index) {
        at(box, row, column, side * 16 + index) = index == bin ? 40.0F : -40.0F;
    }
}

bool near(double actual, double expected, double tolerance = 1e-3)
{
    return std::abs(actual - expected) <= tolerance;
}

/// One person in the cell at row 1, column 0, listed in an order no decoder
/// could guess, under layer names that change with every compilation.
std::vector<Accel::Tensor> onePersonHeads(float score)
{
    Accel::Tensor keypoints{gridTensor("pose/conv9", 51)};
    Accel::Tensor box{gridTensor("pose/conv7", 64)};
    Accel::Tensor scores{gridTensor("pose/conv8", 1, 0.01F)};

    at(scores, 1, 0, 0) = score;
    setDistance(box, 1, 0, 0, 0);
    setDistance(box, 1, 0, 1, 1);
    setDistance(box, 1, 0, 2, 1);
    setDistance(box, 1, 0, 3, 0);
    // The nose: x = (0.25 * 2 + 0) * 32 = 16, y = (0.5 * 2 + 1) * 32 = 64, and
    // a visibility logit of zero, which is one half.
    at(keypoints, 1, 0, 0) = 0.25F;
    at(keypoints, 1, 0, 1) = 0.5F;
    at(keypoints, 1, 0, 2) = 0.0F;
    return {keypoints, box, scores};
}

} // namespace

class TestAcceleratorDecode : public QObject
{
    Q_OBJECT

private slots:
    void poseDecodesBoxAndKeypoints();
    void poseBelowThresholdIsDropped();
    void poseWithoutKeypointTensorIsDropped();
    void poseMapsThroughTheLetterbox();

    void scrfdDecodesBoxAndLandmarks();
    void scrfdBelowThresholdIsDropped();


    void outputNamedMatchesTheLayer();
};

void TestAcceleratorDecode::poseDecodesBoxAndKeypoints()
{
    const QList<PersonDetection> people{
        decodeYoloPoseHeads(onePersonHeads(0.9F), inputSide, LetterboxTransform{}, 0.5F, 0.45F)};
    QCOMPARE(people.size(), 1);
    const PersonDetection &person{people.first()};

    // Cell centre (16, 48); left 0, top 1, right 1 and bottom 0 strides.
    QVERIFY(near(person.box.x, 16.0));
    QVERIFY(near(person.box.y, 16.0));
    QVERIFY(near(person.box.width, 32.0));
    QVERIFY(near(person.box.height, 32.0));
    QVERIFY(near(person.score, 0.9));

    const Keypoint2D &nose{person.keypoints[static_cast<std::size_t>(Keypoint::Nose)]};
    QVERIFY(near(nose.position.x(), 16.0));
    QVERIFY(near(nose.position.y(), 64.0));
    QVERIFY(near(nose.confidence, 0.5));
}

void TestAcceleratorDecode::poseBelowThresholdIsDropped()
{
    QVERIFY(decodeYoloPoseHeads(onePersonHeads(0.3F), inputSide, LetterboxTransform{}, 0.5F, 0.45F)
                .isEmpty());
}

void TestAcceleratorDecode::poseWithoutKeypointTensorIsDropped()
{
    std::vector<Accel::Tensor> heads{onePersonHeads(0.9F)};
    heads.erase(heads.begin());
    QVERIFY(decodeYoloPoseHeads(heads, inputSide, LetterboxTransform{}, 0.5F, 0.45F).isEmpty());
}

void TestAcceleratorDecode::poseMapsThroughTheLetterbox()
{
    // Half scale with an 8 pixel band at the top: input (x, y) is source
    // (2x, 2(y - 8)).
    const LetterboxTransform transform{0.5F, 0.0F, 8.0F};
    const QList<PersonDetection> people{
        decodeYoloPoseHeads(onePersonHeads(0.9F), inputSide, transform, 0.5F, 0.45F)};
    QCOMPARE(people.size(), 1);
    QVERIFY(near(people.first().box.x, 32.0));
    QVERIFY(near(people.first().box.y, 16.0));
    QVERIFY(near(people.first().box.width, 64.0));
    const Keypoint2D &nose{people.first().keypoints[static_cast<std::size_t>(Keypoint::Nose)]};
    QVERIFY(near(nose.position.y(), 112.0));
}

void TestAcceleratorDecode::scrfdDecodesBoxAndLandmarks()
{
    Accel::Tensor box{gridTensor("scrfd/conv42", 8)};
    Accel::Tensor scores{gridTensor("scrfd/conv41", 2)};
    Accel::Tensor landmarks{gridTensor("scrfd/conv43", 20)};

    // The second anchor of the cell at row 0, column 1, whose anchor point is
    // the cell corner (32, 0). Half a stride out on every side.
    at(scores, 0, 1, 1) = 0.9F;
    for (int side{0}; side < 4; ++side) {
        at(box, 0, 1, 4 + side) = 0.5F;
    }
    // Its first landmark a quarter stride right of and below the anchor point.
    at(landmarks, 0, 1, 10) = 0.25F;
    at(landmarks, 0, 1, 11) = 0.25F;

    const QList<FaceDetection> faces{
        decodeScrfd({landmarks, scores, box}, inputSide, LetterboxTransform{}, 0.5F, 0.4F)};
    QCOMPARE(faces.size(), 1);
    QVERIFY(near(faces.first().box.x, 16.0));
    QVERIFY(near(faces.first().box.y, -16.0));
    QVERIFY(near(faces.first().box.width, 32.0));
    QVERIFY(near(faces.first().box.height, 32.0));
    QVERIFY(near(faces.first().landmarks[0].x, 40.0));
    QVERIFY(near(faces.first().landmarks[0].y, 8.0));
    QVERIFY(near(faces.first().score, 0.9));
}

void TestAcceleratorDecode::scrfdBelowThresholdIsDropped()
{
    const QList<FaceDetection> faces{decodeScrfd({gridTensor("scrfd/conv42", 8),
                                                  gridTensor("scrfd/conv41", 2, 0.4F),
                                                  gridTensor("scrfd/conv43", 20)},
                                                 inputSide,
                                                 LetterboxTransform{},
                                                 0.5F,
                                                 0.4F)};
    QVERIFY(faces.isEmpty());
}

void TestAcceleratorDecode::outputNamedMatchesTheLayer()
{
    std::vector<Accel::Tensor> outputs(3);
    outputs[0].info.name = "hand_landmark_lite/fc10";
    outputs[1].info.name = "hand_landmark_lite/fc1";
    outputs[2].info.name = "other/xfc2";

    QCOMPARE(outputNamed(outputs, "fc1"), &outputs[1]);
    QVERIFY(outputNamed(outputs, "fc2") == nullptr);
}

QTEST_GUILESS_MAIN(TestAcceleratorDecode)

#include "tst_acceleratordecode.moc"
