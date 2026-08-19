// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "objectdetector.h"
#include "posedetector.h"
#include "visiontypes.h"

#include <QTest>

#include <opencv2/core.hpp>

#include <vector>

using namespace Kerfur;

namespace {

// 4 box values, 1 confidence, 17 keypoints of (x, y, score).
constexpr int poseChannels{56};
constexpr int poseAnchors{16};

cv::Mat makePoseTensor()
{
    const int sizes[]{1, poseChannels, poseAnchors};
    return cv::Mat{3, sizes, CV_32F, cv::Scalar{0.0}};
}

void setPoseChannel(cv::Mat &tensor, int channel, int anchor, float value)
{
    tensor.ptr<float>()[(static_cast<qsizetype>(channel) * poseAnchors) + anchor] = value;
}

} // namespace

/// The arithmetic between a model's output tensor and a person in the frame.
///
/// This is the only part of the CPU backend whose correctness is decidable
/// without a camera or a model file, and it is also the part where a mistake is
/// invisible: a decode with a wrong stride or a missed letterbox offset still
/// produces plausible boxes in plausible places.
class TestVisionDecode : public QObject
{
    Q_OBJECT

private slots:
    void letterboxPreservesAspectRatio();
    void letterboxMapsBackToSource();
    void decodesOnePersonIntoSourceCoordinates();
    void suppressesOverlappingDetections();
    void rejectsMalformedPoseTensors();
    void decodesNanoDetBoxAndLabel();
};

void TestVisionDecode::letterboxPreservesAspectRatio()
{
    const cv::Mat source{cv::Mat::zeros(360, 640, CV_8UC3)};
    cv::Mat destination;
    const LetterboxTransform transform{letterbox(source, destination, 640)};

    QCOMPARE(destination.cols, 640);
    QCOMPARE(destination.rows, 640);
    QCOMPARE(transform.scale, 1.0F);

    // A 16:9 frame in a square leaves equal bands top and bottom, and none at
    // the sides. Padding the wrong axis is the classic way to get detections
    // that are consistently offset rather than absent.
    QCOMPARE(transform.padX, 0.0F);
    QCOMPARE(transform.padY, 140.0F);
}

void TestVisionDecode::letterboxMapsBackToSource()
{
    const cv::Mat source{cv::Mat::zeros(480, 240, CV_8UC3)};
    cv::Mat destination;
    const LetterboxTransform transform{letterbox(source, destination, 320)};

    // The centre of the source has to survive the round trip, or every box is
    // off by half the padding.
    const cv::Point2f centre{
        transform.toSource((120.0F * transform.scale) + transform.padX,
                           (240.0F * transform.scale) + transform.padY)};
    QVERIFY(qAbs(centre.x - 120.0F) < 0.01F);
    QVERIFY(qAbs(centre.y - 240.0F) < 0.01F);
}

void TestVisionDecode::decodesOnePersonIntoSourceCoordinates()
{
    cv::Mat tensor{makePoseTensor()};
    setPoseChannel(tensor, 0, 3, 110.0F);
    setPoseChannel(tensor, 1, 3, 120.0F);
    setPoseChannel(tensor, 2, 3, 40.0F);
    setPoseChannel(tensor, 3, 3, 60.0F);
    setPoseChannel(tensor, 4, 3, 0.9F);
    setPoseChannel(tensor, 5, 3, 110.0F);
    setPoseChannel(tensor, 6, 3, 100.0F);
    setPoseChannel(tensor, 7, 3, 0.8F);

    const LetterboxTransform transform{0.5F, 10.0F, 20.0F};
    const QList<PersonDetection> detections{decodeYoloPose(tensor, transform, 0.35F, 0.45F)};

    QCOMPARE(detections.size(), 1);
    const PersonDetection &person{detections.first()};

    // Centre 110,120 with extent 40x60 is 90,90 to 130,150 in letterboxed
    // space, which the transform maps to 160,140 through 240,260.
    QVERIFY(qAbs(person.box.x - 160.0F) < 0.01F);
    QVERIFY(qAbs(person.box.y - 140.0F) < 0.01F);
    QVERIFY(qAbs(person.box.width - 80.0F) < 0.01F);
    QVERIFY(qAbs(person.box.height - 120.0F) < 0.01F);

    const Keypoint2D &nose{person.keypoints[static_cast<size_t>(Keypoint::Nose)]};
    QVERIFY(qAbs(nose.position.x() - 200.0) < 0.01);
    QVERIFY(qAbs(nose.position.y() - 160.0) < 0.01);
    QCOMPARE(nose.confidence, 0.8F);
}

void TestVisionDecode::suppressesOverlappingDetections()
{
    cv::Mat tensor{makePoseTensor()};
    for (const int anchor : {1, 2}) {
        setPoseChannel(tensor, 0, anchor, 100.0F + static_cast<float>(anchor));
        setPoseChannel(tensor, 1, anchor, 100.0F);
        setPoseChannel(tensor, 2, anchor, 50.0F);
        setPoseChannel(tensor, 3, anchor, 50.0F);
        setPoseChannel(tensor, 4, anchor, anchor == 1 ? 0.9F : 0.7F);
    }

    // Two anchors two pixels apart are the same person seen by neighbouring
    // grid cells, which is what the whole tensor is full of in practice.
    const QList<PersonDetection> detections{
        decodeYoloPose(tensor, LetterboxTransform{}, 0.35F, 0.45F)};
    QCOMPARE(detections.size(), 1);
    QCOMPARE(detections.first().score, 0.9F);
}

void TestVisionDecode::rejectsMalformedPoseTensors()
{
    // A re-export with a different head would produce a tensor of a different
    // shape, and reading it with these strides would return convincing
    // nonsense. Refusing is the only safe answer.
    const int wrongSizes[]{1, 42, poseAnchors};
    const cv::Mat wrongChannels{3, wrongSizes, CV_32F, cv::Scalar{0.0}};
    QVERIFY(decodeYoloPose(wrongChannels, LetterboxTransform{}, 0.35F, 0.45F).isEmpty());

    const cv::Mat wrongDimensions{cv::Mat::zeros(poseChannels, poseAnchors, CV_32F)};
    QVERIFY(decodeYoloPose(wrongDimensions, LetterboxTransform{}, 0.35F, 0.45F).isEmpty());

    const int sizes[]{1, poseChannels, poseAnchors};
    const cv::Mat wrongType{3, sizes, CV_64F, cv::Scalar{0.0}};
    QVERIFY(decodeYoloPose(wrongType, LetterboxTransform{}, 0.35F, 0.45F).isEmpty());
}

void TestVisionDecode::decodesNanoDetBoxAndLabel()
{
    constexpr int gridWidth{13};
    constexpr int anchors{gridWidth * gridWidth};
    constexpr int classCount{80};
    constexpr int regressionChannels{32};
    constexpr int cupClass{41};

    const int classSizes[]{1, anchors, classCount};
    cv::Mat classTensor{3, classSizes, CV_32F, cv::Scalar{0.0}};
    const int regressionSizes[]{1, anchors, regressionChannels};
    cv::Mat regressionTensor{3, regressionSizes, CV_32F, cv::Scalar{0.0}};

    // Column six, row six of a 13x13 map at stride 32: centre 208, 208.
    constexpr int anchor{(6 * gridWidth) + 6};
    classTensor.ptr<float>()[(static_cast<qsizetype>(anchor) * classCount) + cupClass] = 0.9F;

    // One dominant bin per side puts the expectation on that bin, so every edge
    // sits two bins, or sixty four pixels, from the centre.
    for (int side{0}; side < 4; ++side) {
        regressionTensor.ptr<float>()[(static_cast<qsizetype>(anchor) * regressionChannels)
                                      + (side * 8) + 2] = 40.0F;
    }

    const std::vector<cv::Mat> outputs{classTensor, regressionTensor};
    const QList<ObjectDetection> objects{
        decodeNanoDet(outputs, cv::Size{416, 416}, 0.35F, 0.6F)};

    QCOMPARE(objects.size(), 1);
    QCOMPARE(objects.first().label, QStringLiteral("cup"));
    QVERIFY(qAbs(objects.first().box.x - 144.0F) < 1.0F);
    QVERIFY(qAbs(objects.first().box.y - 144.0F) < 1.0F);
    QVERIFY(qAbs(objects.first().box.width - 128.0F) < 1.0F);
    QVERIFY(qAbs(objects.first().box.height - 128.0F) < 1.0F);
}

QTEST_MAIN(TestVisionDecode)

#include "tst_visiondecode.moc"
