// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "scenegeometry.h"

#include <QStringView>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Kerfur {

namespace {

constexpr double radians(double degrees)
{
    return degrees * std::numbers::pi / 180.0;
}

constexpr double degrees(double radians)
{
    return radians * 180.0 / std::numbers::pi;
}

/// The pose model's yaw is noisy far from frontal, and past this the eyes are
/// so foreshortened that dividing by the cosine magnifies the noise.
constexpr double maximumYawCorrectionDegrees{60.0};

/// An eye spacing below this many pixels is a face too small to measure.
constexpr double minimumEyeSpacingPixels{2.0};

/// A face box shorter than this many pixels is too small to measure.
constexpr double minimumHeadPixels{8.0};

SceneVector scaled(const SceneVector &vector, double factor)
{
    return SceneVector{vector.x * factor, vector.y * factor, vector.z * factor};
}

} // namespace

double fieldOfViewForSensor(const QString &model)
{
    // The Raspberry Pi camera documentation's horizontal figures. A NoIR
    // variant shares its sensor and lens with the plain one.
    const QStringView name{model};
    if (name.startsWith(u"imx708_wide")) {
        return 102.0;
    }
    if (name.startsWith(u"imx708")) {
        return 66.0;
    }
    if (name.startsWith(u"imx219")) {
        return 62.2;
    }
    if (name.startsWith(u"ov5647")) {
        return 53.5;
    }
    return 0.0;
}

QString rangeSourceName(RangeSource source)
{
    switch (source) {
    case RangeSource::Stature:
        return QStringLiteral("stature");
    case RangeSource::Feet:
        return QStringLiteral("feet");
    case RangeSource::Head:
        return QStringLiteral("head");
    case RangeSource::Height:
        return QStringLiteral("height");
    case RangeSource::EyeSpacing:
        return QStringLiteral("eyes");
    case RangeSource::None:
        break;
    }
    return {};
}

SceneGeometry::SceneGeometry(const CameraMount &mount, const QSize &frame)
    : m_mount{mount}
    , m_frame{frame}
    , m_sinTilt{std::sin(radians(mount.tiltDegrees))}
    , m_cosTilt{std::cos(radians(mount.tiltDegrees))}
{
    if (frame.width() > 0 && frame.height() > 0 && mount.horizontalFovDegrees > 1.0 &&
        mount.horizontalFovDegrees < 179.0) {
        m_focalPixels = (static_cast<double>(frame.width()) / 2.0) /
                        std::tan(radians(mount.horizontalFovDegrees) / 2.0);
    }
}

SceneVector SceneGeometry::ray(const QPointF &normalized) const
{
    if (!isValid()) {
        return SceneVector{0.0, 0.0, 1.0};
    }
    return SceneVector{(normalized.x() - 0.5) * m_frame.width() / m_focalPixels,
                       (normalized.y() - 0.5) * m_frame.height() / m_focalPixels,
                       1.0};
}

double SceneGeometry::upOf(const SceneVector &vector) const
{
    return -vector.y * m_cosTilt + vector.z * m_sinTilt;
}

double SceneGeometry::aheadOf(const SceneVector &vector) const
{
    return vector.y * m_sinTilt + vector.z * m_cosTilt;
}

double SceneGeometry::slopeOf(const SceneVector &vector) const
{
    return upOf(vector) / std::hypot(aheadOf(vector), vector.x);
}

bool SceneGeometry::isWhole(const QRectF &box)
{
    return box.width() > 0.0 && box.height() > 0.0 && box.left() >= frameEdgeMargin
           && box.top() >= frameEdgeMargin && box.right() <= 1.0 - frameEdgeMargin
           && box.bottom() <= 1.0 - frameEdgeMargin;
}

Placement SceneGeometry::place(const FaceSighting &sighting) const
{
    if (!isValid()) {
        return {};
    }
    // The whole body first: the angle it spans against a declared height
    // needs neither the camera's height nor the floor, and the floor under the
    // feet comes right after. The face against its own measured size comes
    // next, then the face against a declared height, then the eye spacing,
    // which holds at any angle but carries the spread of adult eye spacings
    // with it. A stranger's assumed height comes last of all, because it is a
    // guess about the person rather than a measurement of them.
    if (const std::optional<Placement> placed{fromStature(sighting)}) {
        return *placed;
    }
    if (const std::optional<Placement> placed{fromFeet(sighting)}) {
        return *placed;
    }
    if (const std::optional<Placement> placed{fromHead(sighting)}) {
        return *placed;
    }
    if (sighting.heightMetres > 0.0) {
        if (const std::optional<Placement> placed{fromHeight(sighting, sighting.heightMetres)}) {
            return *placed;
        }
    }
    if (const std::optional<Placement> placed{fromEyeSpacing(sighting)}) {
        return *placed;
    }
    if (sighting.heightMetres <= 0.0) {
        if (const std::optional<Placement> placed{fromHeight(sighting, defaultHeightMetres)}) {
            return *placed;
        }
    }
    return {};
}

std::optional<Placement> SceneGeometry::fromStature(const FaceSighting &sighting) const
{
    if (sighting.heightMetres <= 0.0 || !sighting.feet.has_value()
        || sighting.feet->y() <= sighting.centre.y()) {
        return std::nullopt;
    }
    // The face stands straight above the ankles, so the two slopes differ by
    // the height between them over the distance along the floor.
    const SceneVector toFace{ray(sighting.centre)};
    const SceneVector toFeet{ray(*sighting.feet)};
    const double spread{slopeOf(toFace) - slopeOf(toFeet)};
    const double rise{sighting.heightMetres - eyesBelowCrownMetres - anklesAboveFloorMetres};
    const double faceAlongFloor{std::hypot(aheadOf(toFace), toFace.x)};
    if (!std::isfinite(spread) || spread <= 0.0 || rise <= 0.0 || faceAlongFloor <= 0.0) {
        return std::nullopt;
    }
    const double floorDistance{rise / spread};
    return finish(scaled(toFace, floorDistance / faceAlongFloor), RangeSource::Stature);
}

std::optional<Placement> SceneGeometry::fromFeet(const FaceSighting &sighting) const
{
    if (!sighting.feet.has_value() || sighting.feet->y() <= sighting.centre.y()) {
        return std::nullopt;
    }
    const SceneVector toFeet{ray(*sighting.feet)};
    const double down{-upOf(toFeet)};
    const double drop{m_mount.heightMetres - anklesAboveFloorMetres};
    if (down <= 0.0 || drop <= 0.0) {
        return std::nullopt;
    }
    const SceneVector feet{scaled(toFeet, drop / down)};
    const double floorDistance{std::hypot(aheadOf(feet), feet.x)};

    // The face stands on that same spot of floor, so it is wherever its own
    // ray reaches the same distance along the floor.
    const SceneVector toFace{ray(sighting.centre)};
    const double faceAlongFloor{std::hypot(aheadOf(toFace), toFace.x)};
    if (faceAlongFloor <= 0.0) {
        return std::nullopt;
    }
    return finish(scaled(toFace, floorDistance / faceAlongFloor), RangeSource::Feet);
}

std::optional<Placement> SceneGeometry::fromHead(const FaceSighting &sighting) const
{
    if (sighting.headMetres <= 0.0 || !isWhole(sighting.box)) {
        return std::nullopt;
    }
    const double pixels{sighting.box.height() * m_frame.height()};
    if (pixels < minimumHeadPixels) {
        return std::nullopt;
    }
    const double depth{m_focalPixels * sighting.headMetres / pixels};
    return finish(scaled(ray(sighting.centre), depth), RangeSource::Head);
}

std::optional<Placement> SceneGeometry::fromHeight(const FaceSighting &sighting,
                                                   double heightMetres) const
{
    const SceneVector toFace{ray(sighting.centre)};
    const double up{upOf(toFace)};
    const double elevation{degrees(std::atan2(up, std::hypot(aheadOf(toFace), toFace.x)))};
    if (std::abs(elevation) < minimumElevationDegrees) {
        return std::nullopt;
    }
    const double rise{heightMetres - eyesBelowCrownMetres - m_mount.heightMetres};
    const double reach{rise / up};
    if (reach <= 0.0) {
        return std::nullopt;
    }
    return finish(scaled(toFace, reach), RangeSource::Height);
}

std::optional<Placement> SceneGeometry::fromEyeSpacing(const FaceSighting &sighting) const
{
    if (!sighting.rightEye.has_value() || !sighting.leftEye.has_value()) {
        return std::nullopt;
    }
    const double dx{(sighting.rightEye->x() - sighting.leftEye->x()) * m_frame.width()};
    const double dy{(sighting.rightEye->y() - sighting.leftEye->y()) * m_frame.height()};
    const double yaw{std::min(std::abs(sighting.yawDegrees), maximumYawCorrectionDegrees)};
    const double spacing{std::hypot(dx, dy) / std::cos(radians(yaw))};
    if (spacing < minimumEyeSpacingPixels) {
        return std::nullopt;
    }
    const double depth{m_focalPixels * eyeSpacingMetres / spacing};
    return finish(scaled(ray(sighting.centre), depth), RangeSource::EyeSpacing);
}

std::optional<Placement> SceneGeometry::finish(const SceneVector &position, RangeSource source) const
{
    Placement placement;
    placement.position = position;
    placement.floorDistanceMetres = std::hypot(aheadOf(position), position.x);
    placement.screenDistanceMetres = std::hypot(position.x,
                                                position.y - m_mount.screenOffsetMetres,
                                                position.z);
    placement.source = source;
    const double fromLens{std::hypot(position.x, position.y, position.z)};
    if (!std::isfinite(placement.floorDistanceMetres) || !std::isfinite(fromLens)
        || fromLens < nearestMetres || placement.floorDistanceMetres > farthestMetres) {
        return std::nullopt;
    }
    return placement;
}

double SceneGeometry::headSizeAt(const FaceSighting &sighting, const Placement &placement) const
{
    if (!isValid() || !placement.isValid() || !isWhole(sighting.box)) {
        return 0.0;
    }
    return sighting.box.height() * m_frame.height() * placement.position.z / m_focalPixels;
}

GazeAngles SceneGeometry::gazeAt(const SceneVector &point) const
{
    // The screen tilts with the camera, so the eyes' own frame is the
    // camera's, moved down the screen to where they are drawn.
    const SceneVector fromEyes{point.x, point.y - m_mount.screenOffsetMetres, point.z};
    return gazeAlong(fromEyes);
}

GazeAngles SceneGeometry::gazeAlong(const SceneVector &direction) const
{
    return GazeAngles{degrees(std::atan2(direction.x, direction.z)),
                      degrees(std::atan2(direction.y, std::hypot(direction.x, direction.z)))};
}

HeadMeasure::HeadMeasure(double minimumMetres, double maximumMetres)
    : m_minimum{minimumMetres}
    , m_maximum{maximumMetres}
{}

void HeadMeasure::add(double metres)
{
    if (!std::isfinite(metres) || metres < m_minimum || metres > m_maximum) {
        return;
    }
    m_samples.append(metres);
    if (m_samples.size() > windowSize) {
        m_samples.removeFirst();
    }
}

std::optional<double> HeadMeasure::result() const
{
    if (m_samples.size() < requiredSamples) {
        return std::nullopt;
    }
    QList<double> sorted{m_samples};
    const auto middle{sorted.begin() + sorted.size() / 2};
    std::nth_element(sorted.begin(), middle, sorted.end());
    return *middle;
}

} // namespace Kerfur
