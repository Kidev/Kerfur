// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "cpuinferencebackend.h"

#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(backendLog, "kerfur.vision.backend")

// A held object is one whose box contains a wrist. Section 5.5 of the design
// makes a COCO detection authoritative for its label wherever it has one, and
// the wrist is what ties that label to a particular person.
constexpr size_t wristIndices[]{static_cast<size_t>(Keypoint::LeftWrist),
                                static_cast<size_t>(Keypoint::RightWrist)};

// The head crop is built from whatever facial keypoints are confident and then
// grown, because those points bound the features rather than the head: a crop
// tight to the eyes and nose loses the chin and the hairline, and YuNet's
// landmarks drift once its input is a face with no edges.
constexpr float headExpansion{1.9F};

// Fallback when no facial keypoint is confident: the top of the person box.
// A person facing away has no usable face, but their head is still there, and
// the detector saying so is cheaper than not looking.
constexpr float headFallbackFraction{0.32F};

QRectF normalized(const cv::Rect2f &box, const cv::Size &frameSize)
{
    if (frameSize.width <= 0 || frameSize.height <= 0) {
        return {};
    }
    const qreal width{static_cast<qreal>(frameSize.width)};
    const qreal height{static_cast<qreal>(frameSize.height)};
    return QRectF{static_cast<qreal>(box.x) / width,
                  static_cast<qreal>(box.y) / height,
                  static_cast<qreal>(box.width) / width,
                  static_cast<qreal>(box.height) / height};
}

} // namespace

CpuInferenceBackend::CpuInferenceBackend(VisionConfig config) : m_config{config} {}

bool CpuInferenceBackend::initialize(const ModelCatalog &catalog,
                                     const QString &galleryDirectory,
                                     const QString &identityCachePath)
{
    if (!catalog.has(ModelId::Pose)) {
        qCWarning(backendLog) << "no pose model in" << catalog.directory()
                              << "- perception cannot start";
        return false;
    }

    if (!m_pose.load(catalog.path(ModelId::Pose))) {
        return false;
    }
    m_pose.setConfidenceThreshold(m_config.poseConfidence);

    if (catalog.has(ModelId::Objects)) {
        if (m_objects.load(catalog.path(ModelId::Objects))) {
            m_objects.setConfidenceThreshold(m_config.objectConfidence);
        }
    }
    if (catalog.has(ModelId::Face)) {
        m_faces.loadDetector(catalog.path(ModelId::Face));
    }
    if (catalog.has(ModelId::Identity)) {
        m_faces.loadRecognizer(catalog.path(ModelId::Identity));
    }
    if (catalog.has(ModelId::Emotion)) {
        m_faces.loadExpression(catalog.path(ModelId::Emotion));
    }

    if (m_faces.hasRecognizer()) {
        m_identities.enrol(m_faces, galleryDirectory, identityCachePath);
    }

    const QStringList missing{catalog.missingStages()};
    if (!missing.isEmpty()) {
        qCInfo(backendLog) << "stages unavailable:" << missing.join(QStringLiteral(", "));
    }
    return true;
}

void CpuInferenceBackend::setOrientationOverride(int degrees)
{
    m_orientation.setManualOverride(degrees);
}

void CpuInferenceBackend::reset()
{
    m_tracker.reset();
    m_orientation.reset();
    m_processedFrames = 0;
}

cv::Mat CpuInferenceBackend::applyOrientation(const cv::Mat &source) const
{
    switch (m_orientation.degrees()) {
    case 90:
        {
            cv::Mat rotated;
            cv::rotate(source, rotated, cv::ROTATE_90_CLOCKWISE);
            return rotated;
        }
    case 180:
        {
            cv::Mat rotated;
            cv::rotate(source, rotated, cv::ROTATE_180);
            return rotated;
        }
    case 270:
        {
            cv::Mat rotated;
            cv::rotate(source, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
            return rotated;
        }
    default:
        // No copy in the common case. The 180 degree rotation the Pi actually
        // needs is free in the sensor there; here it is a full frame copy, so
        // it is worth not paying when the camera is the right way up.
        return source;
    }
}

PerceptionFrame CpuInferenceBackend::process(const VisionFrame &frame)
{
    PerceptionFrame published;
    published.setTimestampMs(frame.timestampMs);
    published.setSequence(frame.sequence);
    published.setOrientationDegrees(m_orientation.degrees());

    if (!isReady() || frame.image.empty()) {
        return published;
    }

    const cv::Mat image{applyOrientation(frame.image)};
    const cv::Size frameSize{image.size()};

    const QList<PersonDetection> detections{m_pose.detect(image)};
    m_tracker.update(detections, frame.timestampMs);

    // Observed after tracking so that the orientation the next frame uses is
    // based on the same evidence the tracker saw, and reported on this frame as
    // the orientation that produced it.
    m_orientation.observe(detections, frame.timestampMs);

    ++m_processedFrames;

    if (m_objects.isLoaded() && m_config.objectStride > 0
        && (m_processedFrames % static_cast<quint64>(m_config.objectStride)) == 0) {
        attributeObjects(m_objects.detect(image));
    }

    if (m_faces.hasDetector()) {
        runFaceStages(image, frame.timestampMs);
    }

    QList<TrackedPerson> people;
    for (const PersonTrack *track : m_tracker.confirmedTracks()) {
        QList<Keypoint2D> keypoints;
        keypoints.reserve(keypointCount);
        for (const Keypoint2D &keypoint : track->keypoints) {
            keypoints.append(Keypoint2D{
                QPointF{keypoint.position.x() / frameSize.width,
                        keypoint.position.y() / frameSize.height},
                keypoint.confidence});
        }

        TrackedPerson person;
        person.setTrackId(track->id);
        person.setBoundingBox(normalized(track->box, frameSize));
        person.setKeypoints(keypoints);
        person.setHeadCenter(headCenterFromKeypoints(keypoints, m_config.keypointConfidence));

        // Proximity as a fraction of frame height, which is what "close" means
        // everywhere in Kerfur: no camera calibration, and monotonic in real
        // distance for a fixed lens.
        person.setProximity(static_cast<qreal>(track->box.height)
                            / static_cast<qreal>(frameSize.height));
        person.setIdentity(track->identity.settled(), track->identity.confidence());
        person.setEmotion(track->emotion, static_cast<qreal>(track->emotionConfidence));
        person.setHeldObjects(track->heldObjects);
        people.append(person);
    }
    published.setPeople(people);
    return published;
}

cv::Rect CpuInferenceBackend::headRegion(const PersonTrack &track, const cv::Size &frameSize) const
{
    float minimumX{std::numeric_limits<float>::max()};
    float minimumY{std::numeric_limits<float>::max()};
    float maximumX{std::numeric_limits<float>::lowest()};
    float maximumY{std::numeric_limits<float>::lowest()};
    int counted{0};

    // Nose, both eyes and both ears. The ears are included here although the
    // head centre excludes them, because a crop wants the extent of the head
    // and a gaze target wants the middle of the face.
    for (int index{static_cast<int>(Keypoint::Nose)};
         index <= static_cast<int>(Keypoint::RightEar);
         ++index) {
        const Keypoint2D &keypoint{track.keypoints[static_cast<size_t>(index)]};
        if (keypoint.confidence < m_config.keypointConfidence) {
            continue;
        }
        minimumX = std::min(minimumX, static_cast<float>(keypoint.position.x()));
        minimumY = std::min(minimumY, static_cast<float>(keypoint.position.y()));
        maximumX = std::max(maximumX, static_cast<float>(keypoint.position.x()));
        maximumY = std::max(maximumY, static_cast<float>(keypoint.position.y()));
        ++counted;
    }

    cv::Rect2f region;
    if (counted == 0) {
        region = cv::Rect2f{track.box.x,
                            track.box.y,
                            track.box.width,
                            track.box.height * headFallbackFraction};
    } else {
        const float centreX{(minimumX + maximumX) / 2.0F};
        const float centreY{(minimumY + maximumY) / 2.0F};
        // Square, because the aspect ratio of a few keypoints says more about
        // which ones were confident than about the shape of the head.
        const float side{std::max(maximumX - minimumX, maximumY - minimumY) * headExpansion};
        const float half{std::max(side, 1.0F) / 2.0F};
        region = cv::Rect2f{centreX - half, centreY - half, half * 2.0F, half * 2.0F};
    }

    const cv::Rect2f bounds{0.0F,
                            0.0F,
                            static_cast<float>(frameSize.width),
                            static_cast<float>(frameSize.height)};
    const cv::Rect2f clipped{region & bounds};
    return cv::Rect{static_cast<int>(std::lround(clipped.x)),
                    static_cast<int>(std::lround(clipped.y)),
                    static_cast<int>(std::lround(clipped.width)),
                    static_cast<int>(std::lround(clipped.height))};
}

void CpuInferenceBackend::runFaceStages(const cv::Mat &image, qint64 timestampMs)
{
    // Nearest first, so that when the per frame budget runs out it is the
    // people furthest away who go without, which is also where the result would
    // have been worth least.
    std::vector<PersonTrack *> candidates;
    for (PersonTrack &track : m_tracker.tracks()) {
        if (!track.confirmed || !track.matchedThisFrame) {
            continue;
        }
        const double proximity{static_cast<double>(track.box.height)
                               / static_cast<double>(image.rows)};
        if (proximity < m_config.closeProximity) {
            continue;
        }
        if ((timestampMs - track.lastFaceAnalysisMs) < m_config.faceIntervalMs) {
            continue;
        }
        candidates.push_back(&track);
    }
    std::sort(candidates.begin(), candidates.end(), [](const PersonTrack *left, const PersonTrack *right) {
        return left->box.height > right->box.height;
    });

    const size_t budget{static_cast<size_t>(std::max(0, m_config.faceAnalysesPerFrame))};
    if (candidates.size() > budget) {
        candidates.resize(budget);
    }

    for (PersonTrack *track : candidates) {
        track->lastFaceAnalysisMs = timestampMs;

        const FaceObservation observation{m_faces.analyse(image, headRegion(*track, image.size()))};
        if (!observation.valid) {
            // A track that had a face and now does not still votes, so that
            // walking out of the camera's reach eventually clears the name
            // rather than freezing it.
            track->identity.observe(QString{}, 0.0);
            continue;
        }

        if (!observation.embedding.empty()) {
            double similarity{0.0};
            const QString name{m_identities.match(observation.embedding, similarity)};
            track->identity.observe(name, similarity);
        }
        if (!observation.emotion.isEmpty()) {
            track->emotion = observation.emotion;
            track->emotionConfidence = observation.emotionConfidence;
        }
    }
}

void CpuInferenceBackend::attributeObjects(const QList<ObjectDetection> &objects)
{
    for (PersonTrack &track : m_tracker.tracks()) {
        track.heldObjects.clear();
    }
    if (objects.isEmpty()) {
        return;
    }

    for (const ObjectDetection &object : objects) {
        // The person class is what the pose stage already answered, and better.
        if (object.label == QStringLiteral("person")) {
            continue;
        }

        for (PersonTrack &track : m_tracker.tracks()) {
            if (!track.confirmed) {
                continue;
            }
            bool held{false};
            for (const size_t wrist : wristIndices) {
                const Keypoint2D &keypoint{track.keypoints[wrist]};
                if (keypoint.confidence < m_config.keypointConfidence) {
                    continue;
                }
                if (object.box.contains(cv::Point2f{static_cast<float>(keypoint.position.x()),
                                                    static_cast<float>(keypoint.position.y())})) {
                    held = true;
                    break;
                }
            }
            if (held && !track.heldObjects.contains(object.label)) {
                track.heldObjects.append(object.label);
            }
        }
    }
}

} // namespace Kerfur
