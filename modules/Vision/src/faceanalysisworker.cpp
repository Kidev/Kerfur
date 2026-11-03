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
#include <utility>

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

// How far beyond the wrist, as a fraction of the forearm, the middle of the
// hand is taken to be. Anatomically the hand's centroid sits at roughly a third
// of a forearm past the wrist, and the pose model puts its wrist keypoint at
// the joint rather than at the base of the palm.
constexpr double handReach{0.34};

// The side of the hand box, as a fraction of the forearm. Hand length is about
// three quarters of forearm length, and the box has to hold a spread hand and
// not just a fist, so it is a little more than that. The box is square because
// the wrist and elbow say nothing about the rotation of the palm.
constexpr double handSpan{0.80};

// When the elbow is not confident, the hand is sized from the person instead.
// A head is about an eighth of a standing adult and a hand is about a head.
constexpr double handFallbackFraction{0.12};

// What a hand measures against the other lengths of the same body.
// Anthropometry, rounded: hand length is about half the width across the
// shoulders, a third of the trunk, three times the distance between the pupils
// and a little over the width across the ears.
//
// These exist because the forearm on its own is a bad ruler in exactly the
// case that matters. An arm reaching towards the camera projects to almost no
// length at all, and a box sized from it is a box far too small to hold the
// hand at the end of it -- which is the hand nearest the lens, the largest one
// in the frame, and the one most likely to be holding something.
constexpr double handPerShoulderWidth{0.50};
constexpr double handPerTorsoHeight{0.35};
constexpr double handPerEyeSpan{3.0};
constexpr double handPerEarSpan{1.30};

// How far the forearm estimate may stray from the body's own scale before it
// is pulled back. Wide, because both rulers foreshorten and neither is being
// trusted to be exact -- this only refuses the answers that cannot be a hand.
constexpr double handShrinkLimit{0.65};
constexpr double handGrowthLimit{1.80};

// How many hands get the landmark pass in one frame. Each costs a palm
// detection and a landmark inference, about ten milliseconds together on a
// desktop and several times that on a Pi, so this is a budget rather than a
// preference. Two is both hands of the person the eyes are already following.
constexpr int maxRefinedHands{2};

// How closed a hand must be before the COCO stage is run on it. Measured open
// hands sit below 0.07 and a closed fist saturates at 1.0, so this sits well
// clear of both. It is only a filter to keep the network off a hand that is
// plainly empty; whether an object is actually held is still decided by
// heldOverlap below, which is the test that can see the object.
constexpr double holdingGrip{0.4};

// How much of the hand must lie inside an object's box before the object counts
// as held. Low, because the box of a phone held end on barely covers a palm,
// and because the alternative reading -- an object merely near a hand -- has
// already been excluded by requiring any overlap at all.
constexpr double heldOverlap{0.18};

// The COCO class that is never a held object. A person standing behind another
// overlaps their hand constantly, and "kidev is holding a person" is the one
// answer this stage must never give.
constexpr QLatin1StringView personClass{"person"};

// How much wider than the hand the crop handed to the object stage is. Wide
// enough for a bottle held by the neck or a book held by one corner, tight
// enough that the thing being held is most of the picture rather than a detail
// in the corner of it.
constexpr double objectCropSpan{3.5};

// The floor and ceiling on that crop, in pixels of the analysed frame. The
// floor buys context for a hand so small that a crop around it would be a
// thumbnail; the ceiling stops a hand near the lens from asking for the whole
// room back, which is the framing this stage exists to get away from.
constexpr int minimumObjectRegion{128};
constexpr double maximumObjectRegionFraction{0.6};

// A detection this much of the crop is the crop. Whatever the network says
// about a box that fills everything it was shown, it is describing the wall
// behind the hand rather than the thing in it.
constexpr double maximumObjectCover{0.85};

// Hands the object stage runs on in one pass, and how far a label has to carry
// a vote of `heldVotes` before it is published. Two out of three: one pass's
// best class over a small crop flickers between the two nearest things it could
// be, and this string is what a skill is written against.
constexpr int maxObjectCrops{2};
constexpr int heldVotesToSettle{2};

// Passes a hold survives without being seen again. At the analysis cadence this
// is a second or so, which is long enough to cover the stride and a turned
// wrist and short enough that a mug set down is not still being held.
constexpr quint64 heldExpiry{12};

// How much of a face box must fall inside a person's head region before the
// two are the same human. Measured against the face, not the region: the
// region is deliberately generous and would dominate a union.
constexpr double faceInsideHead{0.4};

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

/// Identifies one hand of one person across passes. Both halves are needed: a
/// person has two hands, and two people have four between them.
QString heldKey(const QString &identity, const QString &side)
{
    return identity + QLatin1Char('/') + side;
}

QStringList rosterOf(const IdentityRegistry &registry)
{
    QStringList roster;
    roster.reserve(registry.size());
    for (const IdentityRecord &record : registry.records()) {
        roster.append(QStringLiteral("%1 (%2)").arg(record.name, registry.standingOf(record.id)));
    }
    return roster;
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
        m_modelProblems = QStringList{capabilities.problem};
        m_capabilities = capabilities;
        emit ready(capabilities);
        return;
    }
    qCInfo(analysisLog) << "models from" << catalog.directory();

    capabilities.faceReady = m_faces.loadDetector(catalog.path(ModelId::Face));
    m_faces.setDetectionThreshold(static_cast<float>(m_faceConfidence));
    capabilities.identityReady = m_faces.loadRecognizer(catalog.path(ModelId::Identity));
    capabilities.emotionReady = m_faces.loadExpression(catalog.path(ModelId::Emotion));
    capabilities.poseReady = m_pose.load(catalog.path(ModelId::Pose));
    capabilities.objectsReady = m_objects.load(catalog.path(ModelId::Objects));
    // Both or neither: a palm box with no landmark model behind it tells
    // nobody anything the skeleton had not already said.
    capabilities.handsReady = m_handLandmarker.load(catalog.path(ModelId::Palm),
                                                    catalog.path(ModelId::HandLandmarks));

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

    m_modelProblems = problems;
    m_capabilities = capabilities;
    enrolGallery();
}

void FaceAnalysisWorker::reloadGallery()
{
    // Before initialize there is nothing to reload, and initialize enrols
    // whatever is on disk by the time it runs.
    if (!m_initialized) {
        return;
    }
    enrolGallery();
}

void FaceAnalysisWorker::enrolGallery()
{
    AnalysisCapabilities capabilities{m_capabilities};
    capabilities.enrolled.clear();
    capabilities.roster.clear();
    QStringList problems{m_modelProblems};

    // A name voted for under the old gallery is not evidence under the new one.
    m_tracks.clear();
    m_held.clear();

    const QStringList galleries{IdentityDirectory::galleries()};
    capabilities.galleryDirectory = galleries.join(QStringLiteral(", "));
    if (galleries.isEmpty()) {
        m_registry = IdentityRegistry{};
        m_identities.clear();
        problems.append(QStringLiteral("no identities gallery; searched %1")
                            .arg(IdentityDirectory::describeSearchPath()));
    } else {
        m_registry = IdentityRegistry::load(galleries);
        capabilities.enrolled = m_registry.names();
        capabilities.roster = rosterOf(m_registry);
        if (!m_registry.problem().isEmpty()) {
            qCInfo(analysisLog) << m_registry.problem();
        }
        if (m_registry.isEmpty()) {
            m_identities.clear();
            problems.append(QStringLiteral("%1 declares nobody").arg(capabilities.galleryDirectory));
        } else if (capabilities.identityReady) {
            m_identities.enrol(m_faces, m_registry.faceEntries(), identityCachePath());
            qCInfo(analysisLog) << "enrolled" << m_identities.size() << "faces from"
                                << capabilities.galleryDirectory << "roster"
                                << capabilities.roster;
            if (m_identities.isEmpty() && !m_registry.faceEntries().isEmpty()) {
                problems.append(QStringLiteral("no face could be embedded from %1")
                                    .arg(capabilities.galleryDirectory));
            }
        }
    }

    capabilities.problem = problems.join(QStringLiteral("; "));
    m_capabilities = capabilities;
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
            // The skeleton itself moved. The fingerprint below deliberately
            // ignores the keypoints -- they move by a pixel every pass and
            // hashing them would defeat the stride -- so the pose run is what
            // says the bones are new.
            ++m_peopleRevision;
        }
        result.people = m_lastPeople;
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

    // The names first, because who somebody is decides whether their hands are
    // located at all; then the hands, because a measured hand is a better
    // region to search than an extrapolated one and it can say whether the
    // hand is even closed; then the objects, because an object is only held if
    // there is a hand to hold it.
    attributePeople(result, mirrored);
    const bool measured{refineHands(bgr, result)};
    attributeHeldObjects(bgr, result);

    const quint64 current{fingerprint(result.people)};
    // A measured hand is new geometry every pass, unlike the skeleton behind
    // it: twenty-one points that moved, and a wrist joint that moved with them.
    // Without this the landmarks would be published at the pose stride while
    // the picture under them arrives at the frame rate, and a hand drawn two
    // hundred milliseconds behind the hand in the image reads as a hand the
    // models cannot find.
    if (measured || current != m_peopleFingerprint) {
        m_peopleFingerprint = current;
        ++m_peopleRevision;
    }
    result.peopleRevision = m_peopleRevision;

    ++m_passes;
    result.elapsedMs = timer.elapsed();
    emit resultReady(result);
}

void FaceAnalysisWorker::attributePeople(AnalysisResult &result, bool mirrored) const
{
    if (result.people.isEmpty()) {
        return;
    }

    const cv::Size frame{result.frameSize.width(), result.frameSize.height()};

    // Whose left is whose. The pose model labels the arms of the person it can
    // see, and what it can see is a reflection, so the arm it calls left is the
    // one on the subject's right. Undoing that here rather than in the view is
    // the difference between "their right hand" and "the hand on the right of
    // the picture": only the first is a fact about the person, and it is the
    // one a skill will be written against.
    const Keypoint ownLeftWrist{mirrored ? Keypoint::RightWrist : Keypoint::LeftWrist};
    const Keypoint ownLeftElbow{mirrored ? Keypoint::RightElbow : Keypoint::LeftElbow};
    const Keypoint ownRightWrist{mirrored ? Keypoint::LeftWrist : Keypoint::RightWrist};
    const Keypoint ownRightElbow{mirrored ? Keypoint::LeftElbow : Keypoint::RightElbow};

    for (AnalysedPerson &person : result.people) {
        person.identity.clear();
        person.identityConfidence = 0.0;
        person.priority = IdentityRegistry::unknownPriority;
        person.title.clear();
        person.known = false;
        person.leftHand = AnalysedHand{};
        person.rightHand = AnalysedHand{};
        person.held.clear();

        // Whichever named face sits in this person's head region. The region is
        // the same one the face stage was asked to search, so a face found
        // through a person is guaranteed to associate back to them; a face
        // found on the whole frame, because the pose stage saw nobody, has no
        // person to associate with and keeps its name to itself.
        const cv::Rect region{headRegionFromKeypoints(person, frame, poseKeypointConfidence)};
        const QRectF head{normalize(cv::Rect2f{static_cast<float>(region.x),
                                               static_cast<float>(region.y),
                                               static_cast<float>(region.width),
                                               static_cast<float>(region.height)},
                                    frame)};
        double bestOverlap{faceInsideHead};
        for (const AnalysedFace &face : result.faces) {
            if (face.identity.isEmpty()) {
                continue;
            }
            const QRectF intersection{head & face.box};
            const double area{face.box.width() * face.box.height()};
            if (area <= 0.0) {
                continue;
            }
            const double covered{(intersection.width() * intersection.height()) / area};
            if (covered > bestOverlap) {
                bestOverlap = covered;
                person.identity = face.identity;
                person.identityConfidence = face.identityConfidence;
                person.priority = face.priority;
                person.title = face.title;
            }
        }
        person.known = !person.identity.isEmpty();

        // And only now the hands. A stranger's hands are not located, not
        // published and not searched for objects.
        if (!person.known) {
            continue;
        }
        person.leftHand = handFromKeypoints(person,
                                            ownLeftWrist,
                                            ownLeftElbow,
                                            QStringLiteral("left"),
                                            result.frameSize,
                                            poseKeypointConfidence);
        person.rightHand = handFromKeypoints(person,
                                             ownRightWrist,
                                             ownRightElbow,
                                             QStringLiteral("right"),
                                             result.frameSize,
                                             poseKeypointConfidence);
    }
}

bool FaceAnalysisWorker::refineHands(const cv::Mat &bgr, AnalysisResult &result)
{
    if (!m_handLandmarker.isLoaded() || result.people.isEmpty()) {
        return false;
    }

    // Which hands get the expensive pass when more are in view than the budget
    // allows: by rank first and then by nearness, which is the same order the
    // eyes follow. A room with four recognised people therefore costs what a
    // room with one costs, and the hands that are dropped are the ones nobody
    // is looking at.
    struct Candidate
    {
        AnalysedPerson *person;
        AnalysedHand *hand;
        int priority;
        double area;
    };
    QList<Candidate> candidates;
    for (AnalysedPerson &person : result.people) {
        if (!person.known) {
            continue;
        }
        for (AnalysedHand *hand : {&person.leftHand, &person.rightHand}) {
            if (hand->valid) {
                candidates.append(Candidate{&person,
                                            hand,
                                            person.priority,
                                            hand->box.width() * hand->box.height()});
            }
        }
    }
    if (candidates.isEmpty()) {
        return false;
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const Candidate &left, const Candidate &right) {
                  if (left.priority != right.priority) {
                      return left.priority < right.priority;
                  }
                  return left.area > right.area;
              });

    const cv::Size frame{bgr.size()};
    int attempts{0};
    bool measured{false};
    for (const Candidate &candidate : std::as_const(candidates)) {
        if (attempts >= maxRefinedHands) {
            break;
        }
        const QRectF &box{candidate.hand->box};
        const cv::Rect region{cvRound(box.x() * frame.width),
                              cvRound(box.y() * frame.height),
                              cvRound(box.width() * frame.width),
                              cvRound(box.height() * frame.height)};

        const std::optional<HandPose> pose{m_handLandmarker.detect(bgr, region)};
        // The attempt is counted, not the success. The inference was run and
        // paid for whether or not it found a palm, and a frame full of hands
        // the model cannot see must not become a frame that runs the model
        // over and over.
        ++attempts;
        if (!pose) {
            continue;
        }

        AnalysedHand &hand{*candidate.hand};
        hand.box = normalize(pose->box, frame);
        hand.landmarks.clear();
        hand.landmarks.reserve(handPointCount);
        for (const cv::Point2f &point : pose->landmarks) {
            hand.landmarks.append(normalize(point, frame));
        }
        // The measured wrist, which is the base of the palm, rather than the
        // pose stage's wrist joint a little further up the arm.
        hand.wrist = hand.landmarks.at(HandWrist);
        hand.confidence = pose->confidence;
        hand.grip = pose->grip;
        hand.gesture = gestureName(pose->gesture);
        hand.fingers = pose->fingers;
        hand.tracked = true;
        measured = true;

        // And the same point back onto the skeleton, so that the forearm ends
        // where the hand begins instead of a knuckle short of it. The two are
        // the same joint seen by two models -- one that guessed it from a
        // seventeen point body and one that measured it from a cropped hand --
        // and the measurement is the better of the two. The keypoint's score is
        // left alone: it is the pose stage's confidence in having found a wrist
        // at all, which this does not change.
        if (hand.wristKeypoint >= 0 && hand.wristKeypoint < candidate.person->keypoints.size()) {
            candidate.person->keypoints[hand.wristKeypoint] = hand.wrist;
        }
    }
    return measured;
}

void FaceAnalysisWorker::attributeHeldObjects(const cv::Mat &bgr, AnalysisResult &result)
{
    if (!m_objects.isLoaded()) {
        return;
    }

    // A tracked hand that is open is not holding anything, and running an
    // eighty class detector to confirm that is waste. An untracked hand gets
    // the benefit of the doubt: without landmarks there is nothing to judge,
    // and the overlap test below is the real arbiter either way.
    const auto couldHold = [](const AnalysedHand &hand) {
        return hand.valid && (!hand.tracked || hand.grip >= holdingGrip);
    };

    struct Candidate
    {
        AnalysedPerson *person;
        const AnalysedHand *hand;
        int priority;
        double area;
    };
    QList<Candidate> candidates;
    for (AnalysedPerson &person : result.people) {
        if (!person.known) {
            continue;
        }
        for (const AnalysedHand *hand : {&person.leftHand, &person.rightHand}) {
            if (couldHold(*hand)) {
                candidates.append(Candidate{&person,
                                            hand,
                                            person.priority,
                                            hand->box.width() * hand->box.height()});
                continue;
            }
            // Not merely skipped: forgotten. A stale mug that reappears the
            // moment a hand comes back into view would be a memory of an
            // object rather than a sighting of one.
            m_held.remove(heldKey(person.identity, hand->side));
        }
    }

    // The same order the landmark stage spends its budget in, and for the same
    // reason: rank first, then nearness. A room with four recognised people
    // costs what a room with one costs, and what is dropped is what nobody is
    // looking at.
    std::sort(candidates.begin(),
              candidates.end(),
              [](const Candidate &left, const Candidate &right) {
                  if (left.priority != right.priority) {
                      return left.priority < right.priority;
                  }
                  return left.area > right.area;
              });

    const cv::Size frame{bgr.size()};
    const bool detecting{(m_passes % objectStride) == 0};
    int budget{maxObjectCrops};
    for (const Candidate &candidate : std::as_const(candidates)) {
        HeldTrack &track{m_held[heldKey(candidate.person->identity, candidate.hand->side)]};
        if (detecting && budget > 0) {
            --budget;
            track.vote(bestHeldObject(bgr, *candidate.hand, frame), m_passes);
        } else {
            track.lastSeen = m_passes;
        }
        const std::optional<HeldObject> settled{track.settled(m_passes)};
        if (settled) {
            candidate.person->held.append(
                HeldObject{settled->box, settled->label, settled->score, candidate.hand->side});
        }
    }

    // Whoever left the room takes their mug with them.
    for (auto entry{m_held.begin()}; entry != m_held.end();) {
        entry = m_passes - entry->lastSeen > heldExpiry ? m_held.erase(entry) : std::next(entry);
    }
}

std::optional<HeldObject> FaceAnalysisWorker::bestHeldObject(const cv::Mat &bgr,
                                                             const AnalysedHand &hand,
                                                             const cv::Size &frame)
{
    // The detector is shown the hand and its immediate surroundings rather than
    // the whole room. It has one fixed input side, so every pixel spent on the
    // far wall is a pixel not spent on the thing being held: a mug that is
    // thirty pixels across in a 960 wide frame is thirteen by the time the
    // network sees it, which is enough to notice that something is there and
    // nowhere near enough to say what. In a crop it arrives at a hundred and
    // fifty. That single change is the difference between "an object" and "a
    // cup", and it is why this runs per hand instead of once per frame.
    const cv::Rect region{objectRegionForHand(hand, frame)};
    if (region.empty()) {
        return std::nullopt;
    }

    const double regionArea{static_cast<double>(region.width) * region.height};
    const QRectF handBox{hand.box};

    std::optional<HeldObject> best;
    double bestCover{heldOverlap};
    for (const ObjectDetection &detection : m_objects.detect(bgr, region)) {
        // The COCO class that is never a held object. A person standing behind
        // another overlaps their hand constantly, and "kidev is holding a
        // person" is the one answer this stage must never give.
        if (detection.label.compare(QString{personClass}, Qt::CaseInsensitive) == 0) {
            continue;
        }
        // Nor is the crop itself an object. A box that fills what the detector
        // was shown is the detector describing the background it was given, and
        // in a crop taken around a hand the background is furniture.
        if (static_cast<double>(detection.box.area()) > regionArea * maximumObjectCover) {
            continue;
        }
        const double cover{handInsideObject(handBox, normalize(detection.box, frame))};
        if (cover > bestCover) {
            bestCover = cover;
            best = HeldObject{normalize(detection.box, frame),
                              detection.label,
                              detection.score,
                              hand.side};
        }
    }
    return best;
}

void FaceAnalysisWorker::HeldTrack::vote(const std::optional<HeldObject> &observed, quint64 pass)
{
    lastSeen = pass;
    votes[static_cast<size_t>(cursor)] = observed ? observed->label : QString{};
    cursor = (cursor + 1) % static_cast<int>(votes.size());
    count = std::min(count + 1, static_cast<int>(votes.size()));
    if (!observed) {
        return;
    }

    // A label has to have been seen twice in the last three looks before the
    // sighting carrying it is kept. One pass's best class over a small crop
    // flickers between the two nearest things it could be -- a cup and a bowl,
    // a phone and a remote -- and announcing the flicker is worse than
    // announcing nothing: this string is what a skill is written against and
    // what Kerfur says out loud.
    int agreeing{0};
    for (int index{0}; index < count; ++index) {
        agreeing += votes[static_cast<size_t>(index)] == observed->label ? 1 : 0;
    }
    if (agreeing >= heldVotesToSettle) {
        held = *observed;
        heldAt = pass;
    }
}

std::optional<HeldObject> FaceAnalysisWorker::HeldTrack::settled(quint64 pass) const
{
    if (held.label.isEmpty() || pass - heldAt > heldExpiry) {
        return std::nullopt;
    }
    return held;
}

quint64 FaceAnalysisWorker::fingerprint(const QList<AnalysedPerson> &people)
{
    // Everything a consumer draws that is not the skeleton itself. The
    // keypoints are excluded on purpose: they move every frame by a pixel and
    // hashing them would publish a new revision every pass, which is the cost
    // the stride exists to avoid.
    QString summary;
    for (const AnalysedPerson &person : people) {
        summary += person.identity;
        summary += QLatin1Char('|');
        summary += person.leftHand.valid ? QLatin1Char('L') : QLatin1Char('-');
        summary += person.rightHand.valid ? QLatin1Char('R') : QLatin1Char('-');
        for (const HeldObject &object : person.held) {
            summary += QLatin1Char('/');
            summary += object.side;
            summary += object.label;
        }
        summary += QLatin1Char(';');
    }
    return qHash(summary);
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
        face.priority = m_registry.priorityOf(face.identity);
        // Empty for a stranger. standingOf would happily call them
        // "unranked", which is true and is also the manifest speaking about
        // somebody it has never heard of.
        face.title = face.identity.isEmpty() ? QString{} : m_registry.standingOf(face.identity);
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

double bodyHandScale(const AnalysedPerson &person, const QSize &frameSize, float minimumConfidence)
{
    const double width{static_cast<double>(frameSize.width() > 0 ? frameSize.width() : 1)};
    const double height{static_cast<double>(frameSize.height() > 0 ? frameSize.height() : 1)};
    const auto believed = [&person, minimumConfidence](Keypoint keypoint) {
        const int index{static_cast<int>(keypoint)};
        return index < person.keypoints.size() && index < person.keypointScores.size() &&
               person.keypointScores.at(index) >= static_cast<double>(minimumConfidence);
    };
    // In pixels, like every other length here: a normalized distance on a 16:9
    // frame is not a distance.
    const auto span = [&person, width, height](Keypoint from, Keypoint to) {
        const QPointF first{person.keypoints.at(static_cast<int>(from))};
        const QPointF second{person.keypoints.at(static_cast<int>(to))};
        return std::hypot((first.x() - second.x()) * width, (first.y() - second.y()) * height);
    };

    // The largest of whatever is visible, not the first. Every one of these
    // shortens when the body turns and none of them lengthens, so the biggest
    // reading is the one taken most nearly face on and the others are its
    // foreshortened selves.
    double scale{0.0};
    if (believed(Keypoint::LeftShoulder) && believed(Keypoint::RightShoulder)) {
        scale = std::max(scale,
                         span(Keypoint::LeftShoulder, Keypoint::RightShoulder) *
                             handPerShoulderWidth);
        if (believed(Keypoint::LeftHip)) {
            scale = std::max(scale,
                             span(Keypoint::LeftShoulder, Keypoint::LeftHip) * handPerTorsoHeight);
        }
        if (believed(Keypoint::RightHip)) {
            scale = std::max(scale,
                             span(Keypoint::RightShoulder, Keypoint::RightHip) * handPerTorsoHeight);
        }
    }
    if (believed(Keypoint::LeftEye) && believed(Keypoint::RightEye)) {
        scale = std::max(scale, span(Keypoint::LeftEye, Keypoint::RightEye) * handPerEyeSpan);
    }
    if (believed(Keypoint::LeftEar) && believed(Keypoint::RightEar)) {
        scale = std::max(scale, span(Keypoint::LeftEar, Keypoint::RightEar) * handPerEarSpan);
    }
    return scale;
}

AnalysedHand handFromKeypoints(const AnalysedPerson &person,
                               Keypoint wrist,
                               Keypoint elbow,
                               const QString &side,
                               const QSize &frameSize,
                               float minimumConfidence)
{
    AnalysedHand hand;
    hand.side = side;

    const int wristIndex{static_cast<int>(wrist)};
    const int elbowIndex{static_cast<int>(elbow)};
    if (wristIndex >= person.keypoints.size() || wristIndex >= person.keypointScores.size()) {
        return hand;
    }
    const double wristScore{person.keypointScores.at(wristIndex)};
    if (wristScore < static_cast<double>(minimumConfidence)) {
        return hand;
    }

    hand.wrist = person.keypoints.at(wristIndex);
    hand.confidence = wristScore;
    hand.wristKeypoint = wristIndex;

    // Lengths are measured in pixels and only the result is normalized. A
    // normalized distance on a 16:9 frame is not a distance: the same forearm
    // is a different number depending on which way it points, and a hand box
    // built from one comes out oval and wrong on exactly the diagonal reaches
    // that matter.
    const double width{static_cast<double>(frameSize.width() > 0 ? frameSize.width() : 1)};
    const double height{static_cast<double>(frameSize.height() > 0 ? frameSize.height() : 1)};

    QPointF centre{hand.wrist};
    double span{0.0};

    const bool elbowUsable{elbowIndex < person.keypoints.size()
                           && elbowIndex < person.keypointScores.size()
                           && person.keypointScores.at(elbowIndex)
                                  >= static_cast<double>(minimumConfidence)};
    if (elbowUsable) {
        const QPointF elbowPoint{person.keypoints.at(elbowIndex)};
        const double deltaX{(hand.wrist.x() - elbowPoint.x()) * width};
        const double deltaY{(hand.wrist.y() - elbowPoint.y()) * height};
        const double forearm{std::hypot(deltaX, deltaY)};
        if (forearm > 1.0) {
            centre = QPointF{hand.wrist.x() + ((deltaX * handReach) / width),
                             hand.wrist.y() + ((deltaY * handReach) / height)};
            span = forearm * handSpan;
        }
    }

    // The rest of the body, as a second opinion on how big a hand is. It is
    // used two ways: as the whole answer when there is no forearm to measure,
    // and as bounds on the forearm answer when there is, because a foreshortened
    // arm reports a hand a quarter of its true size and the palm detector is
    // then searching a crop the hand does not fit in.
    const double bodyScale{bodyHandScale(person, frameSize, minimumConfidence)};
    if (span > 0.0 && bodyScale > 0.0) {
        span = std::clamp(span, bodyScale * handShrinkLimit, bodyScale * handGrowthLimit);
    } else if (span <= 0.0) {
        span = bodyScale;
    }
    if (span <= 0.0) {
        // No elbow, no torso and no head: everything but the wrist is either
        // out of frame or unbelieved. The person's own height is the only
        // scale left, and it is a better one than a constant because it
        // shrinks with distance the way a hand does.
        span = person.box.height() * height * handFallbackFraction;
    }
    if (span <= 0.0) {
        return hand;
    }

    const double halfX{(span / 2.0) / width};
    const double halfY{(span / 2.0) / height};
    hand.box = QRectF{centre.x() - halfX, centre.y() - halfY, halfX * 2.0, halfY * 2.0};
    hand.valid = true;
    return hand;
}

cv::Rect objectRegionForHand(const AnalysedHand &hand, const cv::Size &frameSize)
{
    if (!hand.valid || frameSize.width <= 0 || frameSize.height <= 0) {
        return {};
    }

    const double frameWidth{static_cast<double>(frameSize.width)};
    const double frameHeight{static_cast<double>(frameSize.height)};
    const double ceiling{std::min(frameWidth, frameHeight) * maximumObjectRegionFraction};
    const double floor{std::min(static_cast<double>(minimumObjectRegion), ceiling)};
    const double side{
        std::clamp(std::max(hand.box.width() * frameWidth, hand.box.height() * frameHeight) *
                       objectCropSpan,
                   floor,
                   ceiling)};
    if (side < 2.0) {
        return {};
    }

    // Slid back inside the frame rather than cut off at it. A hand at the edge
    // of the picture is the common case -- an arm comes into shot before its
    // owner does -- and a crop that loses its right half there hands the
    // network a letterboxed sliver of what it was trained on.
    const double centreX{(hand.box.x() + hand.box.width() / 2.0) * frameWidth};
    const double centreY{(hand.box.y() + hand.box.height() / 2.0) * frameHeight};
    const double left{std::clamp(centreX - side / 2.0, 0.0, std::max(frameWidth - side, 0.0))};
    const double top{std::clamp(centreY - side / 2.0, 0.0, std::max(frameHeight - side, 0.0))};

    return cv::Rect{cvRound(left), cvRound(top), cvRound(side), cvRound(side)} &
           cv::Rect{0, 0, frameSize.width, frameSize.height};
}

double handInsideObject(const QRectF &hand, const QRectF &object)
{
    const double area{hand.width() * hand.height()};
    if (area <= 0.0) {
        return 0.0;
    }
    const QRectF intersection{hand & object};
    if (!intersection.isValid()) {
        return 0.0;
    }
    return (intersection.width() * intersection.height()) / area;
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
