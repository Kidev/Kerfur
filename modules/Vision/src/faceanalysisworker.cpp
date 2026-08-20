// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "faceanalysisworker.h"

#include "headpose.h"
#include "identitydirectory.h"
#include "modelcatalog.h"
#include "modeldirectory.h"

#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStringList>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(analysisLog, "kerfur.vision.analysis")

// How much wider than the confident head keypoints the face crop is taken.
// YuNet wants some margin around the face; a crop that stops at the eyes and
// the nose contains no face at all as far as a detector is concerned.
constexpr float headExpansion{2.6F};

// When the pose stage was confident about none of the head keypoints, the head
// is assumed to be the top of the person box. Roughly a head in a standing
// adult, and generous rather than tight because the cost of a crop that is too
// large is one extra downscale.
constexpr float headFallbackFraction{0.32F};

constexpr float poseKeypointConfidence{0.4F};

// The half width of the window the iris is looked for in, as a fraction of the
// distance between the two eye landmarks. An eye is about a fifth of the
// interocular distance across, and the window has to hold the whole of it plus
// the sclera on either side for the contrast test to mean anything.
constexpr float irisWindowFraction{0.22F};

// A face has to overlap its previous self by this much to be considered the
// same face. Generous, because the alternative to associating two boxes that
// were the same person is starting the identity vote again from nothing.
constexpr double trackOverlap{0.3};

// Passes a face may go unseen before its vote is forgotten. At the analysis
// cadence this is a second or two: long enough to survive a turned head, short
// enough that the next person to sit down does not inherit the last one's name.
constexpr quint64 trackExpiry{20};

cv::Mat qImageToBgr(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    // Format_BGR888 rather than a convert to RGB followed by a cvtColor: the
    // conversion has to happen anyway, and asking Qt for the byte order OpenCV
    // already wants makes it one pass instead of two.
    const QImage source{image.format() == QImage::Format_BGR888
                            ? image
                            : image.convertToFormat(QImage::Format_BGR888)};
    if (source.isNull()) {
        return {};
    }

    const cv::Mat view{source.height(),
                       source.width(),
                       CV_8UC3,
                       const_cast<uchar *>(source.constBits()),
                       static_cast<size_t>(source.bytesPerLine())};
    // Cloned because `source` may be the caller's implicitly shared buffer and
    // will be gone before the stages that read this are done with it.
    return view.clone();
}

QRectF normalize(const cv::Rect2f &box, const cv::Size &frame)
{
    if (frame.width <= 0 || frame.height <= 0) {
        return {};
    }
    return QRectF{static_cast<qreal>(box.x) / frame.width,
                  static_cast<qreal>(box.y) / frame.height,
                  static_cast<qreal>(box.width) / frame.width,
                  static_cast<qreal>(box.height) / frame.height};
}

QPointF normalize(const cv::Point2f &point, const cv::Size &frame)
{
    if (frame.width <= 0 || frame.height <= 0) {
        return {};
    }
    return QPointF{static_cast<qreal>(point.x) / frame.width,
                   static_cast<qreal>(point.y) / frame.height};
}

double intersectionOverUnion(const cv::Rect2f &left, const cv::Rect2f &right)
{
    const float intersection{(left & right).area()};
    const float united{left.area() + right.area() - intersection};
    return united > 0.0F ? static_cast<double>(intersection / united) : 0.0;
}

QString identityCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/identities.json");
}

} // namespace

FaceAnalysisWorker::FaceAnalysisWorker(QObject *parent) : QObject{parent}
{
    m_pose.setConfidenceThreshold(poseKeypointConfidence);
}

void FaceAnalysisWorker::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    AnalysisCapabilities capabilities;
    QStringList problems;

    const ModelCatalog catalog{ModelCatalog::resolve()};
    if (catalog.directory().isEmpty()) {
        capabilities.problem = QStringLiteral("No model directory. Searched %1. "
                                              "Run `make models` to provision one.")
                                   .arg(ModelDirectory::describeSearchPath());
        emit ready(capabilities);
        return;
    }
    qCInfo(analysisLog) << "models from" << catalog.directory();

    capabilities.faceReady = m_faces.loadDetector(catalog.path(ModelId::Face));
    m_faces.setDetectionThreshold(static_cast<float>(m_faceConfidence));
    capabilities.identityReady = m_faces.loadRecognizer(catalog.path(ModelId::Identity));
    capabilities.emotionReady = m_faces.loadExpression(catalog.path(ModelId::Emotion));
    capabilities.poseReady = m_pose.load(catalog.path(ModelId::Pose));

    // The face detector is the only stage whose absence leaves nothing at all
    // to draw. Everything else degrades to a missing field, which the overlay
    // renders as silence rather than as an error.
    if (!capabilities.faceReady) {
        problems.append(QStringLiteral("face detection model missing from %1")
                            .arg(catalog.directory()));
    }
    const QStringList missing{catalog.missingStages()};
    if (!missing.isEmpty()) {
        qCInfo(analysisLog) << "stages without a model file:" << missing;
    }

    const QString gallery{IdentityDirectory::resolve()};
    capabilities.galleryDirectory = gallery;
    if (gallery.isEmpty()) {
        problems.append(QStringLiteral("no identities gallery; searched %1")
                            .arg(IdentityDirectory::describeSearchPath()));
    } else if (capabilities.identityReady) {
        m_identities.enrol(m_faces, gallery, identityCachePath());
        capabilities.enrolled = IdentityDirectory::enrolledNames(gallery);
        qCInfo(analysisLog) << "enrolled" << m_identities.size() << "identities from" << gallery;
        if (m_identities.isEmpty()) {
            problems.append(QStringLiteral("no face could be embedded from %1").arg(gallery));
        }
    }

    capabilities.problem = problems.join(QStringLiteral("; "));
    emit ready(capabilities);
}

void FaceAnalysisWorker::setFaceConfidence(double confidence)
{
    m_faceConfidence = std::clamp(confidence, 0.1, 0.95);
    m_faces.setDetectionThreshold(static_cast<float>(m_faceConfidence));
}

void FaceAnalysisWorker::analyse(const QImage &image, quint64 sequence, bool mirrored)
{
    QElapsedTimer timer;
    timer.start();

    AnalysisResult result;
    result.sequence = sequence;

    // Every path out of here emits. The tracker runs one pass at a time and
    // clears its latch on the result, so a pass that returned in silence would
    // stop the analysis for good rather than skip one frame.
    const cv::Mat bgr{qImageToBgr(image)};
    if (bgr.empty() || (!m_faces.hasDetector() && !m_pose.isLoaded())) {
        result.frameSize = image.size();
        emit resultReady(result);
        return;
    }

    result.frameSize = QSize{bgr.cols, bgr.rows};

    const cv::Size frame{bgr.size()};

    // The pose stage on its own stride, with the previous answer standing in
    // between passes. A skeleton that vanishes every other frame reads as a
    // detection failure; one that is two hundred milliseconds stale does not
    // read as anything at all.
    if (m_pose.isLoaded()) {
        if (m_lastPoseFrame != frame || (m_passes % poseStride) == 0) {
            m_lastPeople.clear();
            for (const PersonDetection &detection : m_pose.detect(bgr)) {
                AnalysedPerson person;
                person.box = normalize(detection.box, frame);
                person.score = detection.score;
                person.keypoints.reserve(keypointCount);
                person.keypointScores.reserve(keypointCount);
                for (const Keypoint2D &keypoint : detection.keypoints) {
                    person.keypoints.append(QPointF{keypoint.position.x() / frame.width,
                                                    keypoint.position.y() / frame.height});
                    person.keypointScores.append(keypoint.confidence);
                }
                m_lastPeople.append(person);
            }
            m_lastPoseFrame = frame;
            ++m_peopleRevision;
        }
        result.people = m_lastPeople;
        result.peopleRevision = m_peopleRevision;
    }

    if (m_faces.hasDetector()) {
        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

        QList<cv::Rect> regions;
        if (!result.people.isEmpty()) {
            // Largest first, so that the cap falls on whoever is furthest away
            // rather than on whoever the detector happened to list last.
            QList<AnalysedPerson> ordered{result.people};
            std::sort(ordered.begin(),
                      ordered.end(),
                      [](const AnalysedPerson &left, const AnalysedPerson &right) {
                          return left.box.width() * left.box.height()
                                 > right.box.width() * right.box.height();
                      });
            for (const AnalysedPerson &person : ordered) {
                if (regions.size() >= maxFaces) {
                    break;
                }
                regions.append(headRegionFromKeypoints(person, frame, poseKeypointConfidence));
            }
        }
        if (regions.isEmpty()) {
            // Nobody from the pose stage, either because it is not provisioned
            // or because it saw no whole person. A face in front of the lens is
            // still a face, and it is the case the camera view exists for.
            regions.append(cv::Rect{0, 0, frame.width, frame.height});
        }

        for (const cv::Rect &region : regions) {
            const FaceObservation observation{m_faces.analyse(bgr, region)};
            if (!observation.valid) {
                continue;
            }
            result.faces.append(describeFace(bgr, gray, observation, mirrored));
        }
    }

    ++m_passes;
    result.elapsedMs = timer.elapsed();
    emit resultReady(result);
}

AnalysedFace FaceAnalysisWorker::describeFace(const cv::Mat &bgr,
                                              const cv::Mat &gray,
                                              const FaceObservation &observation,
                                              bool mirrored)
{
    const cv::Size frame{bgr.size()};

    AnalysedFace face;
    face.box = normalize(observation.box, frame);
    face.emotion = observation.emotion;
    face.emotionConfidence = observation.emotionConfidence;
    face.landmarks.reserve(faceLandmarkCount);
    for (const cv::Point2f &landmark : observation.landmarks) {
        face.landmarks.append(normalize(landmark, frame));
    }

    if (!observation.embedding.empty() && !m_identities.isEmpty()) {
        double similarity{0.0};
        const QString observed{m_identities.match(observation.embedding, similarity)};
        double confidence{0.0};
        face.identity = voteIdentity(observation.box, observed, similarity, m_passes, confidence);
        face.identityConfidence = confidence;
    }

    // The preview is a mirror, so what the detector called the subject's right
    // eye is on the subject's left. Undoing the reflection here, and only here,
    // is what lets the overlay keep drawing in the coordinates it can see while
    // the pose solver gets the handedness it requires.
    const auto flip = [&](const cv::Point2f &point) {
        return mirrored ? cv::Point2f{static_cast<float>(frame.width - 1) - point.x, point.y}
                        : point;
    };
    std::array<cv::Point2f, faceLandmarkCount> upright{};
    if (mirrored) {
        upright = {flip(observation.landmarks[1]),
                   flip(observation.landmarks[0]),
                   flip(observation.landmarks[2]),
                   flip(observation.landmarks[4]),
                   flip(observation.landmarks[3])};
    } else {
        upright = observation.landmarks;
    }
    face.pose = estimateHeadPose(upright, frame);

    // The irises are searched in the image as captured, because that is where
    // the pixels are, and only their meaning is translated afterwards.
    //
    // A mirror is the easy case and it is worth saying why: it is what makes
    // the subject's own right appear on the right of the picture, so an offset
    // measured there is already subject-centric and needs no sign change. The
    // unmirrored camera image is the one that has to be flipped. What the
    // mirror does change is which landmark is which eye, exactly as it does for
    // the pose.
    const cv::Point2f lowerX{observation.landmarks[0]};
    const cv::Point2f higherX{observation.landmarks[1]};
    const float interocular{static_cast<float>(cv::norm(higherX - lowerX))};
    if (interocular > 0.0F) {
        const float radius{interocular * irisWindowFraction};
        EyeGaze atLowerX{estimateIris(gray, lowerX, radius)};
        EyeGaze atHigherX{estimateIris(gray, higherX, radius)};
        if (!mirrored) {
            atLowerX.offset.setX(-atLowerX.offset.x());
            atHigherX.offset.setX(-atHigherX.offset.x());
        }
        face.rightEye = mirrored ? atHigherX : atLowerX;
        face.leftEye = mirrored ? atLowerX : atHigherX;
    }

    face.gazeDirection = describeGaze(face.pose, face.rightEye, face.leftEye);
    return face;
}

QString FaceAnalysisWorker::voteIdentity(const cv::Rect2f &box,
                                         const QString &observed,
                                         double similarity,
                                         quint64 sequence,
                                         double &confidence)
{
    m_tracks.removeIf([sequence](const FaceTrack &track) {
        return sequence - track.lastSeen > trackExpiry;
    });

    FaceTrack *chosen{nullptr};
    double bestOverlap{trackOverlap};
    for (FaceTrack &track : m_tracks) {
        const double overlap{intersectionOverUnion(track.box, box)};
        if (overlap >= bestOverlap) {
            bestOverlap = overlap;
            chosen = &track;
        }
    }
    if (chosen == nullptr) {
        m_tracks.append(FaceTrack{});
        chosen = &m_tracks.last();
    }

    chosen->box = box;
    chosen->lastSeen = sequence;
    chosen->votes[static_cast<size_t>(chosen->voteCursor)] = observed;
    chosen->voteCursor = (chosen->voteCursor + 1) % identityVotes;
    chosen->voteCount = std::min(chosen->voteCount + 1, identityVotes);

    // A plurality of the window, with the unknown answer voting like any other.
    // Requiring a majority instead would leave a face that the gallery half
    // recognises permanently nameless, which is worse than a name that takes
    // five frames to appear.
    QHash<QString, int> tally;
    for (int index{0}; index < chosen->voteCount; ++index) {
        ++tally[chosen->votes[static_cast<size_t>(index)]];
    }

    QString leader;
    int leading{0};
    for (auto entry{tally.cbegin()}; entry != tally.cend(); ++entry) {
        if (entry.value() > leading) {
            leading = entry.value();
            leader = entry.key();
        }
    }

    chosen->settled = leader;
    chosen->settledConfidence = leader.isEmpty() || leader != observed
                                    ? chosen->settledConfidence
                                    : similarity;
    confidence = leader.isEmpty() ? 0.0 : chosen->settledConfidence;
    return leader;
}

cv::Rect headRegionFromKeypoints(const AnalysedPerson &person,
                                 const cv::Size &frameSize,
                                 float minimumConfidence)
{
    if (frameSize.width <= 0 || frameSize.height <= 0) {
        return {};
    }

    const auto denormalize = [&frameSize](const QPointF &point) {
        return cv::Point2f{static_cast<float>(point.x() * frameSize.width),
                           static_cast<float>(point.y() * frameSize.height)};
    };

    float minimumX{std::numeric_limits<float>::max()};
    float minimumY{std::numeric_limits<float>::max()};
    float maximumX{std::numeric_limits<float>::lowest()};
    float maximumY{std::numeric_limits<float>::lowest()};
    int counted{0};

    // Nose, both eyes and both ears: the extent of the head rather than the
    // middle of the face, because this is a crop and not a gaze target.
    const int lastHeadKeypoint{static_cast<int>(Keypoint::RightEar)};
    for (int index{static_cast<int>(Keypoint::Nose)}; index <= lastHeadKeypoint; ++index) {
        if (index >= person.keypoints.size() || index >= person.keypointScores.size()) {
            break;
        }
        if (person.keypointScores.at(index) < minimumConfidence) {
            continue;
        }
        const cv::Point2f point{denormalize(person.keypoints.at(index))};
        minimumX = std::min(minimumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumX = std::max(maximumX, point.x);
        maximumY = std::max(maximumY, point.y);
        ++counted;
    }

    cv::Rect2f region;
    if (counted == 0) {
        region = cv::Rect2f{static_cast<float>(person.box.x() * frameSize.width),
                            static_cast<float>(person.box.y() * frameSize.height),
                            static_cast<float>(person.box.width() * frameSize.width),
                            static_cast<float>(person.box.height() * frameSize.height
                                               * headFallbackFraction)};
    } else {
        const float centreX{(minimumX + maximumX) / 2.0F};
        const float centreY{(minimumY + maximumY) / 2.0F};
        // Square, because the aspect ratio of a few keypoints says more about
        // which of them were confident than about the shape of the head.
        const float side{std::max(maximumX - minimumX, maximumY - minimumY) * headExpansion};
        const float half{std::max(side, 1.0F) / 2.0F};
        region = cv::Rect2f{centreX - half, centreY - half, half * 2.0F, half * 2.0F};
    }

    const cv::Rect2f clipped{region
                             & cv::Rect2f{0.0F,
                                          0.0F,
                                          static_cast<float>(frameSize.width),
                                          static_cast<float>(frameSize.height)}};
    return cv::Rect{static_cast<int>(std::lround(clipped.x)),
                    static_cast<int>(std::lround(clipped.y)),
                    static_cast<int>(std::lround(clipped.width)),
                    static_cast<int>(std::lround(clipped.height))};
}

} // namespace Kerfur
