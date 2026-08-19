// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "facepipeline.h"

#include <QLoggingCategory>
#include <QStringList>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(faceLog, "kerfur.vision.face")

constexpr int alignedFaceSide{112};

// The ArcFace reference points for a 112x112 crop, in the same order YuNet
// emits its landmarks: the subject's right eye, left eye, nose tip, right mouth
// corner, left mouth corner. No permutation is needed between the two, which is
// the one thing worth checking before trusting an alignment.
const cv::Point2f arcFaceTemplate[]{{38.2946F, 51.6963F},
                                    {73.5318F, 51.5014F},
                                    {56.0252F, 71.7366F},
                                    {41.5493F, 92.3655F},
                                    {70.7299F, 92.2041F}};

constexpr int landmarkCount{5};
// x, y, width, height, then five landmark pairs, then the score.
constexpr int detectionColumns{15};
constexpr int landmarkOffset{4};

constexpr float detectorScoreThreshold{0.7F};
constexpr float detectorNmsThreshold{0.3F};
constexpr int detectorTopK{64};

// A head crop smaller than this is not worth a face model: YuNet is trained
// down to roughly ten pixels, but a face that small produces landmarks too
// noisy for the alignment, and a bad alignment is worse than no identity.
constexpr int minimumRoiSide{32};

// YuNet detects faces of roughly ten to three hundred pixels, measured at its
// own input resolution. A crop is therefore scaled so its longer side lands
// here before detection: a headshot handed over at full resolution contains a
// face far larger than three hundred pixels and is not detected at all, which
// is a silent failure rather than a poor result.
constexpr int detectorMaxSide{320};

} // namespace

const QStringList &FacePipeline::emotionLabels()
{
    // The order the classifier scores them in. Reordering this silently
    // relabels every emotion Kerfur has ever reported.
    static const QStringList labels{QStringLiteral("angry"),
                                    QStringLiteral("disgust"),
                                    QStringLiteral("fearful"),
                                    QStringLiteral("happy"),
                                    QStringLiteral("neutral"),
                                    QStringLiteral("sad"),
                                    QStringLiteral("surprised")};
    return labels;
}

bool FacePipeline::loadDetector(const QString &modelPath)
{
    m_detector.reset();
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_detectorInputSize = cv::Size{320, 320};
        m_detector = cv::FaceDetectorYN::create(modelPath.toStdString(),
                                                std::string{},
                                                m_detectorInputSize,
                                                detectorScoreThreshold,
                                                detectorNmsThreshold,
                                                detectorTopK);
    } catch (const cv::Exception &error) {
        qCWarning(faceLog) << "cannot read face detector" << modelPath << error.what();
        m_detector.reset();
        return false;
    }
    return !m_detector.empty();
}

bool FacePipeline::loadRecognizer(const QString &modelPath)
{
    m_recognizer.reset();
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_recognizer = cv::FaceRecognizerSF::create(modelPath.toStdString(), std::string{});
    } catch (const cv::Exception &error) {
        qCWarning(faceLog) << "cannot read face recognizer" << modelPath << error.what();
        m_recognizer.reset();
        return false;
    }
    return !m_recognizer.empty();
}

bool FacePipeline::loadExpression(const QString &modelPath)
{
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_expression = cv::dnn::readNetFromONNX(modelPath.toStdString());
    } catch (const cv::Exception &error) {
        qCWarning(faceLog) << "cannot read expression model" << modelPath << error.what();
        return false;
    }
    if (m_expression.empty()) {
        return false;
    }

    m_expression.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    m_expression.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    return true;
}

cv::Mat FacePipeline::detectLargestFace(const cv::Mat &image)
{
    if (m_detector.empty() || image.empty()) {
        return {};
    }

    const int longestSide{std::max(image.cols, image.rows)};
    double scale{1.0};
    cv::Mat input{image};
    if (longestSide > detectorMaxSide) {
        scale = static_cast<double>(detectorMaxSide) / static_cast<double>(longestSide);
        cv::resize(image, input, cv::Size{}, scale, scale, cv::INTER_AREA);
    }

    // YuNet rebuilds its anchor grid on every size change, so this is set only
    // when the size actually differs. A head crop changes size every frame,
    // which would otherwise make the reconfiguration the dominant cost of the
    // whole face path.
    if (input.size() != m_detectorInputSize) {
        m_detectorInputSize = input.size();
        m_detector->setInputSize(m_detectorInputSize);
    }

    cv::Mat faces;
    m_detector->detect(input, faces);
    if (faces.empty() || faces.cols != detectionColumns) {
        return {};
    }

    // Back into `image` coordinates, so that every caller works in one space
    // and the alignment reads its landmarks from the full resolution crop
    // rather than from the reduced one.
    if (scale != 1.0) {
        const float inverse{static_cast<float>(1.0 / scale)};
        for (int row{0}; row < faces.rows; ++row) {
            for (int column{0}; column < detectionColumns - 1; ++column) {
                faces.at<float>(row, column) *= inverse;
            }
        }
    }

    // The largest face only. With one head per crop there is normally one row,
    // but a shoulder or a poster behind the subject produces a second, and the
    // big one is the one the pose stage pointed at. Nothing downstream has ever
    // wanted the others, and returning a sorted copy of all of them meant
    // copying rows between matrices for no reader.
    int best{0};
    float bestArea{faces.at<float>(0, 2) * faces.at<float>(0, 3)};
    for (int row{1}; row < faces.rows; ++row) {
        const float area{faces.at<float>(row, 2) * faces.at<float>(row, 3)};
        if (area > bestArea) {
            bestArea = area;
            best = row;
        }
    }
    return faces.row(best).clone();
}

FaceObservation FacePipeline::analyse(const cv::Mat &bgr, const cv::Rect &roi)
{
    FaceObservation observation;
    if (bgr.empty() || m_detector.empty()) {
        return observation;
    }

    const cv::Rect bounded{roi & cv::Rect{0, 0, bgr.cols, bgr.rows}};
    if (bounded.width < minimumRoiSide || bounded.height < minimumRoiSide) {
        return observation;
    }

    try {
        const cv::Mat crop{bgr(bounded)};
        const cv::Mat faces{detectLargestFace(crop)};
        if (faces.empty()) {
            return observation;
        }

        // One row, but in crop coordinates. Everything downstream of here works
        // on the crop, and only the reported box is lifted back to the frame.
        cv::Mat row{faces.row(0).clone()};
        const cv::Mat aligned{alignFace(crop, row)};
        if (aligned.empty()) {
            return observation;
        }

        observation.box = cv::Rect2f{row.at<float>(0, 0) + static_cast<float>(bounded.x),
                                     row.at<float>(0, 1) + static_cast<float>(bounded.y),
                                     row.at<float>(0, 2),
                                     row.at<float>(0, 3)};
        observation.valid = true;

        if (!m_recognizer.empty()) {
            cv::Mat feature;
            m_recognizer->feature(aligned, feature);
            // The recognizer hands back a view onto its own buffer, which the
            // next call overwrites. Cloning here is what stops every enrolled
            // identity from becoming whatever was embedded last.
            observation.embedding = feature.clone();
        }

        if (!m_expression.empty()) {
            cv::Mat rgb;
            cv::cvtColor(aligned, rgb, cv::COLOR_BGR2RGB);
            cv::Mat normalized;
            rgb.convertTo(normalized, CV_32FC3, 1.0 / 255.0);
            normalized -= cv::Scalar::all(0.5);
            normalized /= 0.5;

            m_expression.setInput(cv::dnn::blobFromImage(normalized));
            const cv::Mat logits{m_expression.forward()};
            const QStringList &labels{emotionLabels()};
            if (logits.total() == static_cast<size_t>(labels.size())) {
                const float *const scores{logits.ptr<float>()};
                const int best{static_cast<int>(
                    std::distance(scores,
                                  std::max_element(scores, scores + labels.size())))};

                // The model emits raw logits, so a softmax is needed before the
                // number means anything. Reporting the bare logit as a
                // confidence would give the behaviour engine a value that is
                // not comparable between frames.
                float total{0.0F};
                for (qsizetype index{0}; index < labels.size(); ++index) {
                    total += std::exp(scores[index] - scores[best]);
                }
                observation.emotion = labels.at(best);
                observation.emotionConfidence = total > 0.0F ? 1.0F / total : 0.0F;
            }
        }
        return observation;
    } catch (const cv::Exception &error) {
        qCWarning(faceLog) << "face stage failed" << error.what();
        return FaceObservation{};
    }
}

std::optional<cv::Mat> FacePipeline::embedLargestFace(const cv::Mat &bgr)
{
    if (bgr.empty() || m_detector.empty() || m_recognizer.empty()) {
        return std::nullopt;
    }

    try {
        const cv::Mat faces{detectLargestFace(bgr)};
        if (faces.empty()) {
            return std::nullopt;
        }
        const cv::Mat aligned{alignFace(bgr, faces.row(0))};
        if (aligned.empty()) {
            return std::nullopt;
        }
        cv::Mat feature;
        m_recognizer->feature(aligned, feature);
        return feature.clone();
    } catch (const cv::Exception &error) {
        qCWarning(faceLog) << "enrolment failed" << error.what();
        return std::nullopt;
    }
}

cv::Mat alignFace(const cv::Mat &image, const cv::Mat &faceRow)
{
    if (image.empty() || faceRow.empty() || faceRow.cols < detectionColumns
        || faceRow.type() != CV_32F) {
        return {};
    }

    std::vector<cv::Point2f> landmarks;
    std::vector<cv::Point2f> reference;
    landmarks.reserve(landmarkCount);
    reference.reserve(landmarkCount);
    for (int index{0}; index < landmarkCount; ++index) {
        landmarks.emplace_back(faceRow.at<float>(0, landmarkOffset + (index * 2)),
                               faceRow.at<float>(0, landmarkOffset + (index * 2) + 1));
        reference.push_back(arcFaceTemplate[index]);
    }

    // Partial affine: rotation, uniform scale and translation only. A full
    // affine would let the shear absorb a bad landmark and produce a crop that
    // looks aligned while having moved the features off the reference points.
    const cv::Mat transform{cv::estimateAffinePartial2D(landmarks, reference, cv::noArray(),
                                                        cv::LMEDS)};
    if (transform.empty()) {
        return {};
    }

    cv::Mat aligned;
    cv::warpAffine(image,
                   aligned,
                   transform,
                   cv::Size{alignedFaceSide, alignedFaceSide},
                   cv::INTER_LINEAR);
    return aligned;
}

} // namespace Kerfur
