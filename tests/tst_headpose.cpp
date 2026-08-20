// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "headpose.h"

#include <QTest>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace Kerfur;

namespace {

constexpr double degreesToRadians{CV_PI / 180.0};

/// Projects the canonical head under a known subject-centric rotation.
///
/// This is the only honest way to test a pose solver without a calibrated rig:
/// build the picture the geometry says a head in that pose would make, and
/// require the solver to name the pose back. A test that fed it landmarks and
/// asserted the numbers it happened to produce would pass for any sign
/// convention, including the wrong one.
std::array<cv::Point2f, faceLandmarkCount> project(double yaw,
                                                   double pitch,
                                                   double roll,
                                                   const cv::Size &frame)
{
    const double x{-pitch * degreesToRadians};
    const double y{yaw * degreesToRadians};
    const double z{-roll * degreesToRadians};

    const cv::Matx33d aboutX{1, 0, 0, 0, std::cos(x), -std::sin(x), 0, std::sin(x), std::cos(x)};
    const cv::Matx33d aboutY{std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y)};
    const cv::Matx33d aboutZ{std::cos(z), -std::sin(z), 0, std::sin(z), std::cos(z), 0, 0, 0, 1};
    const cv::Matx33d rotation{aboutZ * aboutY * aboutX};

    const double focal{static_cast<double>(frame.width)};
    const cv::Matx33d camera{focal, 0.0,   frame.width / 2.0,
                             0.0,   focal, frame.height / 2.0,
                             0.0,   0.0,   1.0};
    const cv::Vec3d translation{0.0, 0.0, 700.0};

    std::array<cv::Point2f, faceLandmarkCount> points{};
    const auto &model{canonicalHeadModel()};
    for (size_t index{0}; index < model.size(); ++index) {
        const cv::Vec3d object{model[index].x, model[index].y, model[index].z};
        const cv::Vec3d inCamera{rotation * object + translation};
        const cv::Vec3d projected{camera * inCamera};
        points[index] = cv::Point2f{static_cast<float>(projected[0] / projected[2]),
                                    static_cast<float>(projected[1] / projected[2])};
    }
    return points;
}

} // namespace

class TestHeadPose : public QObject
{
    Q_OBJECT

private slots:
    void recoversKnownRotations_data()
    {
        QTest::addColumn<double>("yaw");
        QTest::addColumn<double>("pitch");
        QTest::addColumn<double>("roll");

        QTest::newRow("frontal") << 0.0 << 0.0 << 0.0;
        QTest::newRow("turned to their right") << 25.0 << 0.0 << 0.0;
        QTest::newRow("turned to their left") << -25.0 << 0.0 << 0.0;
        QTest::newRow("chin up") << 0.0 << 15.0 << 0.0;
        QTest::newRow("chin down") << 0.0 << -15.0 << 0.0;
        QTest::newRow("tilted right") << 0.0 << 0.0 << 20.0;
        QTest::newRow("tilted left") << 0.0 << 0.0 << -20.0;
        QTest::newRow("all three") << 30.0 << 10.0 << -12.0;
    }

    void recoversKnownRotations()
    {
        QFETCH(double, yaw);
        QFETCH(double, pitch);
        QFETCH(double, roll);

        const cv::Size frame{960, 540};
        const HeadPose pose{estimateHeadPose(project(yaw, pitch, roll, frame), frame)};

        QVERIFY(pose.valid);
        QVERIFY2(std::abs(pose.yaw - yaw) < 1.0, qPrintable(QString::number(pose.yaw)));
        QVERIFY2(std::abs(pose.pitch - pitch) < 1.0, qPrintable(QString::number(pose.pitch)));
        QVERIFY2(std::abs(pose.roll - roll) < 1.0, qPrintable(QString::number(pose.roll)));
    }

    /// The signs are stated from the subject's point of view, and the picture
    /// they make is the opposite way round. Pinning that here is what stops a
    /// well meaning sign flip in the solver from silently reversing every
    /// direction Kerfur reports.
    void turningRightMovesTheNoseLeftInThePicture()
    {
        const cv::Size frame{960, 540};
        const auto frontal{project(0.0, 0.0, 0.0, frame)};
        const auto turned{project(30.0, 0.0, 0.0, frame)};

        const auto noseOffset = [](const std::array<cv::Point2f, faceLandmarkCount> &points) {
            return points[2].x - ((points[0].x + points[1].x) / 2.0F);
        };
        QVERIFY(noseOffset(turned) < noseOffset(frontal));
    }

    void eyesSitAboveTheMouth()
    {
        const cv::Size frame{960, 540};
        const auto points{project(0.0, 0.0, 0.0, frame)};
        const float eyes{(points[0].y + points[1].y) / 2.0F};
        const float mouth{(points[3].y + points[4].y) / 2.0F};
        QVERIFY(eyes < mouth);
    }

    /// The subject's right eye is the one on the left of an unmirrored picture.
    /// A model that had these the other way round would still recover the
    /// synthetic rotations above, because it would be self consistent, and
    /// would be a mirror of the truth on every real face.
    void theRightEyeIsOnTheLeftOfThePicture()
    {
        const cv::Size frame{960, 540};
        const auto points{project(0.0, 0.0, 0.0, frame)};
        QVERIFY(points[0].x < points[1].x);
    }

    void degenerateInputIsRefused()
    {
        std::array<cv::Point2f, faceLandmarkCount> collapsed{};
        collapsed.fill(cv::Point2f{100.0F, 100.0F});
        QVERIFY(!estimateHeadPose(collapsed, cv::Size{0, 0}).valid);
    }

    void irisNeedsAnEye()
    {
        // Flat grey has no iris in it, and the surround test is what must say
        // so rather than returning whichever pixel happened to be darkest.
        const cv::Mat flat{cv::Mat{80, 160, CV_8UC1, cv::Scalar::all(128)}};
        QVERIFY(!estimateIris(flat, cv::Point2f{80.0F, 40.0F}, 30.0F).valid);
    }

    void irisIsFoundWhereItWasDrawn()
    {
        cv::Mat eye{cv::Mat{80, 200, CV_8UC1, cv::Scalar::all(200)}};
        // Off centre and to the picture's right by a quarter of the window.
        cv::circle(eye, cv::Point{115, 40}, 7, cv::Scalar::all(20), -1);

        const EyeGaze gaze{estimateIris(eye, cv::Point2f{100.0F, 40.0F}, 30.0F)};
        QVERIFY(gaze.valid);
        QVERIFY2(gaze.offset.x() > 0.2, qPrintable(QString::number(gaze.offset.x())));
        QVERIFY(std::abs(gaze.offset.y()) < 0.3);
    }

    /// A dark line across the window is a spectacle rim, not an iris. This is
    /// the case that made an earlier centroid based version report a confident
    /// upward gaze for anyone wearing glasses.
    void aSpectacleRimIsNotAnIris()
    {
        cv::Mat eye{cv::Mat{80, 200, CV_8UC1, cv::Scalar::all(200)}};
        cv::line(eye, cv::Point{0, 28}, cv::Point{199, 28}, cv::Scalar::all(10), 3);

        QVERIFY(!estimateIris(eye, cv::Point2f{100.0F, 40.0F}, 30.0F).valid);
    }

    void gazePhraseNeedsBothEyes()
    {
        HeadPose pose;
        pose.valid = true;

        EyeGaze right;
        right.valid = true;
        right.offset = QPointF{0.5, 0.0};
        const EyeGaze absent;

        QCOMPARE(describeGaze(pose, right, absent), QStringLiteral("at the camera"));

        EyeGaze left{right};
        QVERIFY(describeGaze(pose, right, left).contains(QStringLiteral("eyes right")));
    }

    void anInvalidPoseDescribesNothing()
    {
        QVERIFY(describeGaze(HeadPose{}, EyeGaze{}, EyeGaze{}).isEmpty());
    }
};

QTEST_MAIN(TestHeadPose)
#include "tst_headpose.moc"
