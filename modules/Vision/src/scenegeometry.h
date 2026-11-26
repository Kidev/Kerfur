// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SCENEGEOMETRY_H
#define KERFUR_SCENEGEOMETRY_H

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <optional>

namespace Kerfur {

/// Where the camera sits and how it looks out.
///
/// The screen is taken to be mounted with the camera: it has the camera's tilt,
/// and its centre (where the eyes are drawn) lies `screenOffsetMetres` below
/// the camera along the screen. A negative offset puts the camera below the
/// screen centre.
struct CameraMount
{
    /// Height of the lens above the floor.
    double heightMetres{1.0};
    /// Zero looks straight ahead, ninety straight up, negative values down.
    double tiltDegrees{0.0};
    double screenOffsetMetres{0.1};
    /// Across the full width of the frame the analysis sees.
    double horizontalFovDegrees{70.0};
};

/// The horizontal field of view of a Raspberry Pi camera module, by the sensor
/// model libcamera reports, or 0 for a sensor whose lens is not fixed or not
/// known.
double fieldOfViewForSensor(const QString &model);

/// A point or a direction in the camera frame, in metres for a point: `x`
/// towards the right of the mirrored picture (the viewer's right, facing the
/// screen), `y` down the picture, `z` out of the lens.
struct SceneVector
{
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

/// How a distance was measured, best first.
enum class RangeSource {
    None,
    /// The whole body against the person's declared height: the angle from
    /// the ankles up to the face.
    Stature,
    /// The ankles, projected onto the floor.
    Feet,
    /// The face against the size this person's face was measured at.
    Head,
    /// The face, projected onto the plane at the person's eye height.
    Height,
    /// The distance between the eyes, against an average adult's.
    EyeSpacing,
};

/// `stature`, `feet`, `head`, `height`, `eyes`, or empty for `None`.
QString rangeSourceName(RangeSource source);

/// What one pass says about one face, normalized to the analysed (mirrored)
/// frame.
struct FaceSighting
{
    QPointF centre;
    /// The face detector's box, or an empty one when there is none.
    QRectF box;
    /// The two eye landmarks, when the detector gave them.
    std::optional<QPointF> rightEye;
    std::optional<QPointF> leftEye;
    /// Head yaw in degrees, which foreshortens the eye spacing; 0 when unknown.
    double yawDegrees{0.0};
    /// The midpoint of the ankles of the body this face belongs to, when the
    /// pose stage saw them.
    std::optional<QPointF> feet;
    /// The person's declared standing height, or 0 when there is none.
    double heightMetres{0.0};
    /// How tall the face detector's box is on this person, in metres, or 0
    /// when it was never measured.
    double headMetres{0.0};
};

/// Where a face is.
struct Placement
{
    /// The face, in the camera frame.
    SceneVector position;
    /// Along the floor, from the point below the camera to the point below the
    /// face.
    double floorDistanceMetres{0.0};
    /// In a straight line, from the centre of the screen to the face.
    double screenDistanceMetres{0.0};
    RangeSource source{RangeSource::None};

    bool isValid() const { return source != RangeSource::None; }
};

/// Which way the drawn eyes have to turn, from the centre of the screen, in
/// degrees. Yaw is positive towards the viewer's right, pitch positive
/// downwards.
struct GazeAngles
{
    double yawDegrees{0.0};
    double pitchDegrees{0.0};
};

/// The pinhole model of one camera mount and one analysed frame size.
///
/// Lens distortion is ignored: on a wide lens a face near the edge of the
/// picture is placed a little too far out.
class SceneGeometry
{
public:
    SceneGeometry(const CameraMount &mount, const QSize &frame);

    /// False for an empty frame or a field of view outside (1, 179) degrees,
    /// in which case nothing can be placed.
    bool isValid() const { return m_focalPixels > 0.0; }

    /// The direction through a normalized point, with `z` equal to one.
    SceneVector ray(const QPointF &normalized) const;

    /// The best placement the sighting allows, or one whose source is `None`.
    Placement place(const FaceSighting &sighting) const;

    /// How tall the face's box is in metres, at the depth `placement` puts it,
    /// or 0 when the box is missing or cut off by the edge of the frame.
    double headSizeAt(const FaceSighting &sighting, const Placement &placement) const;

    /// The turn from the screen centre to a point in the camera frame.
    GazeAngles gazeAt(const SceneVector &point) const;

    /// The turn towards something too far away for the offset between the
    /// camera and the screen to matter.
    GazeAngles gazeAlong(const SceneVector &direction) const;

    /// How high above the floor, and how far ahead along it, a camera frame
    /// vector reaches.
    double upOf(const SceneVector &vector) const;
    double aheadOf(const SceneVector &vector) const;

    /// A standing height for somebody whose own is not known.
    static constexpr double defaultHeightMetres{1.70};
    /// From the top of the head down to the eyes.
    static constexpr double eyesBelowCrownMetres{0.12};
    /// From the floor up to the ankle joint the pose model marks.
    static constexpr double anklesAboveFloorMetres{0.08};
    /// An average adult's distance between the pupils.
    static constexpr double eyeSpacingMetres{0.063};
    /// Below this angle above or below the horizon, a ray meets the plane at
    /// eye height too far from where it meets it a few centimetres higher for
    /// the height to say anything.
    static constexpr double minimumElevationDegrees{8.0};
    /// A placement closer to the lens than this, or farther along the floor
    /// than that, is discarded.
    static constexpr double nearestMetres{0.1};
    static constexpr double farthestMetres{25.0};
    /// A face box closer than this to an edge of the frame, as a fraction of
    /// the frame, may be cut off by it and does not say how large the face is.
    static constexpr double frameEdgeMargin{0.01};

private:
    std::optional<Placement> fromStature(const FaceSighting &sighting) const;
    std::optional<Placement> fromFeet(const FaceSighting &sighting) const;
    std::optional<Placement> fromHead(const FaceSighting &sighting) const;
    std::optional<Placement> fromHeight(const FaceSighting &sighting, double heightMetres) const;
    std::optional<Placement> fromEyeSpacing(const FaceSighting &sighting) const;
    /// Fills in the floor distance, and refuses a placement out of range.
    std::optional<Placement> finish(const SceneVector &position, RangeSource source) const;
    /// The tangent of the angle above the horizon of a camera frame vector.
    double slopeOf(const SceneVector &vector) const;
    /// Whether the face box lies whole inside the frame.
    static bool isWhole(const QRectF &box);

    CameraMount m_mount;
    QSize m_frame;
    double m_focalPixels{0.0};
    double m_sinTilt{0.0};
    double m_cosTilt{1.0};
};

/// The size of one person's face, settled from several measurements.
///
/// Each measurement is the face box's height at a distance taken from the
/// whole body. A measurement outside the range a face can have is dropped,
/// and the result is the median of the last `windowSize`, given once
/// `requiredSamples` have been kept, so one bad frame moves nothing.
class HeadMeasure
{
public:
    static constexpr qsizetype requiredSamples{15};
    static constexpr qsizetype windowSize{31};

    /// `minimumMetres` and `maximumMetres` bound a plausible face box height.
    HeadMeasure(double minimumMetres, double maximumMetres);

    void add(double metres);
    qsizetype count() const { return m_samples.size(); }
    /// The median, once there are `requiredSamples`.
    std::optional<double> result() const;

private:
    double m_minimum{0.0};
    double m_maximum{0.0};
    QList<double> m_samples;
};

} // namespace Kerfur

#endif // KERFUR_SCENEGEOMETRY_H
