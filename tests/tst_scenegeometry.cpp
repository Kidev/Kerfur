// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "scenegeometry.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace Kerfur;

namespace {

/// A square frame and a ninety degree lens give a focal length of half the
/// width, which keeps the expected values below easy to check by hand.
const QSize frame{1000, 1000};
constexpr double focal{500.0};

CameraMount levelMount(double height)
{
    CameraMount mount;
    mount.heightMetres = height;
    mount.tiltDegrees = 0.0;
    mount.screenOffsetMetres = 0.1;
    mount.horizontalFovDegrees = 90.0;
    return mount;
}

/// Where a point `ahead` metres in front of the camera along the floor,
/// `right` to the viewer's right and at `height` above the floor appears in the
/// frame, for any tilt. The inverse of what the geometry does, written out
/// separately so that the two can be checked against each other.
QPointF project(const CameraMount &mount, double ahead, double right, double height)
{
    const double tilt{mount.tiltDegrees * std::numbers::pi / 180.0};
    const double up{height - mount.heightMetres};
    const double y{ahead * std::sin(tilt) - up * std::cos(tilt)};
    const double z{ahead * std::cos(tilt) + up * std::sin(tilt)};
    const double f{(frame.width() / 2.0) /
                   std::tan(mount.horizontalFovDegrees * std::numbers::pi / 360.0)};
    return QPointF{0.5 + (right / z) * f / frame.width(), 0.5 + (y / z) * f / frame.height()};
}

bool near(double actual, double expected, double tolerance = 1e-6)
{
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

class TestSceneGeometry : public QObject
{
    Q_OBJECT

private slots:
    void knownSensorsHaveAFieldOfView();
    void unknownSensorsHaveNone();

    void centreRayIsTheAxis();
    void noPlacementWithoutAFieldOfView();

    void feetPlaceAPersonOnTheFloor();
    void feetAboveTheFaceAreIgnored();
    void declaredHeightPlacesAFaceSeenFromBelow();
    void declaredHeightIsRefusedNearTheHorizon();
    void eyeSpacingPlacesAFaceAtAnyHeight();
    void eyeSpacingIsCorrectedForATurnedHead();
    void tooSmallAFaceIsNotPlaced();
    void tiltedCameraPlacesFeetOnTheFloor();

    void gazeLooksUpFromBelowTheCamera();
    void distantGazeFollowsTheRay();
};

void TestSceneGeometry::knownSensorsHaveAFieldOfView()
{
    QCOMPARE(fieldOfViewForSensor(QStringLiteral("imx708_wide")), 102.0);
    QCOMPARE(fieldOfViewForSensor(QStringLiteral("imx708_wide_noir")), 102.0);
    QCOMPARE(fieldOfViewForSensor(QStringLiteral("imx708")), 66.0);
}

void TestSceneGeometry::unknownSensorsHaveNone()
{
    QCOMPARE(fieldOfViewForSensor(QStringLiteral("imx477")), 0.0);
    QCOMPARE(fieldOfViewForSensor(QString{}), 0.0);
}

void TestSceneGeometry::centreRayIsTheAxis()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    const SceneVector axis{geometry.ray(QPointF{0.5, 0.5})};
    QCOMPARE(axis.x, 0.0);
    QCOMPARE(axis.y, 0.0);
    QCOMPARE(axis.z, 1.0);
    // Ninety degrees across the frame: the edge is forty five degrees out.
    QVERIFY(near(geometry.ray(QPointF{1.0, 0.5}).x, 1.0));
}

void TestSceneGeometry::noPlacementWithoutAFieldOfView()
{
    CameraMount mount{levelMount(1.0)};
    mount.horizontalFovDegrees = 0.0;
    const SceneGeometry geometry{mount, frame};
    QVERIFY(!geometry.isValid());
    FaceSighting sighting;
    sighting.centre = QPointF{0.5, 0.4};
    sighting.feet = QPointF{0.5, 0.8};
    QVERIFY(!geometry.place(sighting).isValid());
}

void TestSceneGeometry::feetPlaceAPersonOnTheFloor()
{
    const CameraMount mount{levelMount(1.0)};
    const SceneGeometry geometry{mount, frame};
    FaceSighting sighting;
    // Two metres ahead: ankles 0.92 m below the lens, eyes 0.6 m above it.
    sighting.feet = QPointF{0.5, 0.5 + (0.92 / 2.0) * focal / frame.height()};
    sighting.centre = QPointF{0.5, 0.5 - (0.6 / 2.0) * focal / frame.height()};

    const Placement placement{geometry.place(sighting)};
    QVERIFY(placement.source == RangeSource::Feet);
    QVERIFY(near(placement.floorDistanceMetres, 2.0));
    QVERIFY(near(placement.position.z, 2.0));
    QVERIFY(near(placement.position.y, -0.6));
}

void TestSceneGeometry::feetAboveTheFaceAreIgnored()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    FaceSighting sighting;
    sighting.centre = QPointF{0.5, 0.7};
    sighting.feet = QPointF{0.5, 0.6};
    QVERIFY(geometry.place(sighting).source != RangeSource::Feet);
}

void TestSceneGeometry::declaredHeightPlacesAFaceSeenFromBelow()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    FaceSighting sighting;
    // Eyes at 1.70 m, so 0.7 m above the lens; two metres ahead.
    sighting.heightMetres = 1.70 + SceneGeometry::eyesBelowCrownMetres;
    sighting.centre = QPointF{0.5, 0.5 - (0.7 / 2.0) * focal / frame.height()};

    const Placement placement{geometry.place(sighting)};
    QVERIFY(placement.source == RangeSource::Height);
    QVERIFY(near(placement.floorDistanceMetres, 2.0));
}

void TestSceneGeometry::declaredHeightIsRefusedNearTheHorizon()
{
    const SceneGeometry geometry{levelMount(1.58), frame};
    FaceSighting sighting;
    // Level with the lens: the plane at eye height is the plane of the ray.
    sighting.heightMetres = 1.70;
    sighting.centre = QPointF{0.5, 0.5};
    QVERIFY(!geometry.place(sighting).isValid());

    // The same face with its eyes visible falls back to their spacing.
    const double spacing{focal * SceneGeometry::eyeSpacingMetres / 2.0 / frame.width()};
    sighting.rightEye = QPointF{0.5 - spacing / 2.0, 0.5};
    sighting.leftEye = QPointF{0.5 + spacing / 2.0, 0.5};
    const Placement placement{geometry.place(sighting)};
    QVERIFY(placement.source == RangeSource::EyeSpacing);
    QVERIFY(near(placement.floorDistanceMetres, 2.0));
}

void TestSceneGeometry::eyeSpacingPlacesAFaceAtAnyHeight()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    FaceSighting sighting;
    sighting.centre = QPointF{0.5, 0.5};
    const double spacing{focal * SceneGeometry::eyeSpacingMetres / 3.0 / frame.width()};
    sighting.rightEye = QPointF{0.5 - spacing / 2.0, 0.5};
    sighting.leftEye = QPointF{0.5 + spacing / 2.0, 0.5};

    const Placement placement{geometry.place(sighting)};
    QVERIFY(placement.source == RangeSource::EyeSpacing);
    QVERIFY(near(placement.floorDistanceMetres, 3.0));
}

void TestSceneGeometry::eyeSpacingIsCorrectedForATurnedHead()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    FaceSighting sighting;
    sighting.centre = QPointF{0.5, 0.5};
    // Turned sixty degrees, the eyes look half as far apart.
    const double spacing{focal * SceneGeometry::eyeSpacingMetres / 3.0 / frame.width() / 2.0};
    sighting.rightEye = QPointF{0.5 - spacing / 2.0, 0.5};
    sighting.leftEye = QPointF{0.5 + spacing / 2.0, 0.5};
    sighting.yawDegrees = 60.0;

    QVERIFY(near(geometry.place(sighting).floorDistanceMetres, 3.0));
}

void TestSceneGeometry::tooSmallAFaceIsNotPlaced()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    FaceSighting sighting;
    sighting.centre = QPointF{0.5, 0.5};
    sighting.rightEye = QPointF{0.5, 0.5};
    sighting.leftEye = QPointF{0.5005, 0.5};
    sighting.heightMetres = 1.70;
    QVERIFY(!geometry.place(sighting).isValid());
}

void TestSceneGeometry::tiltedCameraPlacesFeetOnTheFloor()
{
    CameraMount mount{levelMount(1.5)};
    mount.tiltDegrees = -30.0;
    const SceneGeometry geometry{mount, frame};
    FaceSighting sighting;
    sighting.feet = project(mount, 3.0, 0.4, SceneGeometry::anklesAboveFloorMetres);
    sighting.centre = project(mount, 3.0, 0.4, 1.6);

    const Placement placement{geometry.place(sighting)};
    QVERIFY(placement.source == RangeSource::Feet);
    QVERIFY(near(placement.floorDistanceMetres, std::hypot(3.0, 0.4)));
    QVERIFY(near(geometry.upOf(placement.position), 1.6 - 1.5));
}

void TestSceneGeometry::gazeLooksUpFromBelowTheCamera()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    // A face one metre ahead at the height of the lens is above the eyes,
    // which are drawn ten centimetres lower.
    const GazeAngles gaze{geometry.gazeAt(SceneVector{0.0, 0.0, 1.0})};
    QVERIFY(near(gaze.yawDegrees, 0.0));
    QVERIFY(near(gaze.pitchDegrees, -std::atan2(0.1, 1.0) * 180.0 / std::numbers::pi));

    const GazeAngles right{geometry.gazeAt(SceneVector{1.0, 0.1, 1.0})};
    QVERIFY(near(right.yawDegrees, 45.0));
    QVERIFY(near(right.pitchDegrees, 0.0));
}

void TestSceneGeometry::distantGazeFollowsTheRay()
{
    const SceneGeometry geometry{levelMount(1.0), frame};
    const GazeAngles gaze{geometry.gazeAlong(geometry.ray(QPointF{1.0, 0.5}))};
    QVERIFY(near(gaze.yawDegrees, 45.0));
    QVERIFY(near(gaze.pitchDegrees, 0.0));
}

QTEST_APPLESS_MAIN(TestSceneGeometry)

#include "tst_scenegeometry.moc"
