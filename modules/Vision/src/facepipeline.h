// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_FACEPIPELINE_H
#define KERFUR_FACEPIPELINE_H

#include "accelerator.h"

#include <QList>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect/face.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace Kerfur {

/// What stages C, D and E concluded about one face.
///
/// `embedding` is empty when the identity model is not provisioned, and the
/// emotion fields are empty when the expression model is not. Every stage here
/// is independently optional, because the wasm profile ships three of the five
/// models and has to degrade to exactly the capabilities it has.
struct FaceObservation
{
    cv::Rect2f box;

    /// The five YuNet points in the same frame coordinates as `box`: the
    /// subject's right eye, left eye, nose tip, right mouth corner, left mouth
    /// corner. Carried out of the stage rather than recomputed because the
    /// alignment already had them, and because head pose and iris search are
    /// the two consumers that cannot work from a box alone.
    std::array<cv::Point2f, 5> landmarks{};

    cv::Mat embedding;
    QString emotion;
    float emotionConfidence{0.0F};
    bool valid{false};
};

/// Stages C, D and E: find the face, align it once, and run whatever is loaded
/// against that one aligned crop.
///
/// The single alignment is the point. Both the recognition and the expression
/// model want the same 112x112 canonical face, and producing it twice would
/// double the most expensive part of the face path for nothing.
class FacePipeline
{
public:
    bool loadDetector(const QString &modelPath);
    bool loadRecognizer(const QString &modelPath);
    bool loadExpression(const QString &modelPath);

    /// Detection and recognition on `device`, from compiled SCRFD and ArcFace
    /// models. Each is true when the model opened, in which case the matching
    /// OpenCV load is not needed. The expression model has no compiled form
    /// and stays on the CPU either way.
    bool loadAcceleratedDetector(Accel::Device &device, const QString &modelPath);
    bool loadAcceleratedRecognizer(Accel::Device &device, const QString &modelPath);

    /// The score a detection must reach. Applied to the live detector rather
    /// than stored for the next load, because the camera view puts this on a
    /// slider and a value that only takes effect on restart reads as a slider
    /// that does nothing.
    void setDetectionThreshold(float threshold);

    bool hasDetector() const { return !m_detector.empty() || m_acceleratedDetector != nullptr; }
    bool hasRecognizer() const
    {
        return !m_recognizer.empty() || m_acceleratedRecognizer != nullptr;
    }
    bool hasExpression() const { return !m_expression.empty(); }

    /// Whether the expression model runs on each face. Off until asked for.
    void setExpressionEnabled(bool enabled) { m_expressionEnabled = enabled; }

    /// True when the detector takes the whole frame in one pass, which the
    /// compiled SCRFD does: one 640 pass finds every face in the room for less
    /// than three head crops cost on the CPU. `analyseFrame` is then the call.
    bool detectsWholeFrame() const { return m_acceleratedDetector != nullptr; }

    /// The compiled recogniser's file name without extension, or empty for
    /// SFace. Embeddings of two models are not comparable, so the gallery
    /// cache is kept per model.
    QString recognizerName() const { return m_recognizerName; }

    /// Runs the face stages inside `roi`, which the caller derives from the
    /// pose keypoints. Searching a head crop rather than the whole frame is
    /// both cheaper and more reliable, for the same reason the Pi runs SCRFD
    /// that way: the pose stage has already said where a head is.
    FaceObservation analyse(const cv::Mat &bgr, const cv::Rect &roi);

    /// Every face in `bgr`, largest first, at most `maxFaces` of them, each
    /// with the same stages `analyse` runs. Needs `detectsWholeFrame`.
    QList<FaceObservation> analyseFrame(const cv::Mat &bgr, int maxFaces);

    /// Whole image variant, used to enrol the gallery under `assets/faces`.
    std::optional<cv::Mat> embedLargestFace(const cv::Mat &bgr);

    static const QStringList &emotionLabels();

private:
    /// The largest face YuNet found, as one row of its `1 x 15` detection
    /// format in `image` coordinates, or an empty matrix.
    cv::Mat detectLargestFace(const cv::Mat &image);

    /// Every face the compiled detector found in `image`, as YuNet rows,
    /// largest first. Empty when it found none or failed.
    std::vector<cv::Mat> detectAccelerated(const cv::Mat &image);

    /// Aligns the face of `row` (in `image` coordinates), runs recognition
    /// and expression on it, and reports it shifted by `offset`.
    FaceObservation describe(const cv::Mat &image, const cv::Mat &row, const cv::Point2f &offset);

    /// The embedding of one aligned face, or an empty matrix.
    cv::Mat embed(const cv::Mat &aligned);

    void reportFailure(const std::string &error);

    cv::Ptr<cv::FaceDetectorYN> m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_recognizer;
    cv::dnn::Net m_expression;
    cv::Size m_detectorInputSize;
    std::unique_ptr<Accel::Model> m_acceleratedDetector;
    std::unique_ptr<Accel::Model> m_acceleratedRecognizer;
    std::vector<Accel::Tensor> m_outputs;
    QString m_recognizerName;
    float m_detectionThreshold{0.7F};
    bool m_expressionEnabled{false};
    bool m_reportedFailure{false};
};

/// Warps `image` onto the canonical 112x112 face using the five landmarks in a
/// YuNet detection row.
///
/// This reproduces `cv::FaceRecognizerSF::alignCrop` exactly, verified to a
/// mean absolute difference of zero, and exists so that the expression stage
/// does not have to load a 39 MB recognition model purely to crop an image.
cv::Mat alignFace(const cv::Mat &image, const cv::Mat &faceRow);

} // namespace Kerfur

#endif // KERFUR_FACEPIPELINE_H
