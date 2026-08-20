// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "handlandmarker.h"

#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace Kerfur {

Q_LOGGING_CATEGORY(handLog, "kerfur.vision.hand")

namespace {

/// Where the palm graph's seven landmarks put the two points the rotation is
/// measured between: the base of the palm and the base of the middle finger.
constexpr int palmBaseIndex{0};
constexpr int middleFingerBaseIndex{2};

/// Upstream's box adjustments, kept under their own names so the arithmetic
/// below can be read against mp_handpose.py without a decoder ring.
constexpr float palmPreEnlarge{4.0F};
constexpr float palmEnlarge{3.0F};
constexpr float palmShiftY{-0.4F};
constexpr float handBoxShiftY{-0.1F};
constexpr float handBoxEnlarge{1.65F};

/// Both graphs came out of TensorFlow and want NHWC. cv::dnn::blobFromImage
/// cannot be asked for that layout - it always emits NCHW - so the blob is
/// built by hand. For a single image NHWC is byte for byte the interleaved
/// HWC buffer already in the Mat, so this is a reshape and a copy, not a
/// transpose.
cv::Mat blobFromRgb(const cv::Mat &rgbFloat)
{
    CV_Assert(rgbFloat.type() == CV_32FC3 && rgbFloat.isContinuous());
    const int sizes[4]{1, rgbFloat.rows, rgbFloat.cols, 3};
    return cv::Mat{4, sizes, CV_32F, rgbFloat.data}.clone();
}

/// The palm graph's letterbox: aspect preserving, centred, black bars.
struct Letterbox
{
    cv::Mat image;
    /// The bars, expressed in source pixels, which is the unit the decode
    /// subtracts them in.
    cv::Point2f padBias;
};

Letterbox letterboxForPalm(const cv::Mat &bgr)
{
    const float side{static_cast<float>(HandLandmarker::palmInputSide)};
    const float ratio{std::min(side / static_cast<float>(bgr.rows), side / static_cast<float>(bgr.cols))};
    const int width{static_cast<int>(static_cast<float>(bgr.cols) * ratio)};
    const int height{static_cast<int>(static_cast<float>(bgr.rows) * ratio)};

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size{std::max(width, 1), std::max(height, 1)});

    const int padWidth{HandLandmarker::palmInputSide - resized.cols};
    const int padHeight{HandLandmarker::palmInputSide - resized.rows};
    const int left{padWidth / 2};
    const int top{padHeight / 2};

    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, top, padHeight - top, left, padWidth - left,
                       cv::BORDER_CONSTANT, cv::Scalar{});

    // Truncated to whole pixels, which is what the reference implementation
    // does. The exact value carries a fractional part and dropping it shifts
    // every decoded point by up to a pixel, so this is deliberately bug for bug
    // rather than more accurate: the decode is checked against upstream's
    // numbers, and a silent improvement here would make that check meaningless.
    return {padded,
            cv::Point2f{static_cast<float>(static_cast<int>(static_cast<float>(left) / ratio)),
                        static_cast<float>(static_cast<int>(static_cast<float>(top) / ratio))}};
}

/// A crop taken around a palm box, squared off by padding so that rotating it
/// cannot push a corner outside the image.
struct PalmCrop
{
    cv::Mat image;
    /// The box actually cropped, after the shift, the enlargement and the clip.
    cv::Rect2f box;
    /// Crop coordinates plus this equals input coordinates.
    cv::Point2f bias;
    bool valid{false};
};

PalmCrop cropAroundPalm(const cv::Mat &image, const cv::Rect2f &palmBox, bool forRotation)
{
    const float shiftY{forRotation ? 0.0F : palmShiftY};
    const float enlarge{forRotation ? palmPreEnlarge : palmEnlarge};

    const cv::Point2f centre{palmBox.x + palmBox.width / 2.0F,
                             palmBox.y + palmBox.height / 2.0F + shiftY * palmBox.height};
    const cv::Point2f half{palmBox.width * enlarge / 2.0F, palmBox.height * enlarge / 2.0F};

    const int left{std::clamp(static_cast<int>(centre.x - half.x), 0, image.cols)};
    const int top{std::clamp(static_cast<int>(centre.y - half.y), 0, image.rows)};
    const int right{std::clamp(static_cast<int>(centre.x + half.x), 0, image.cols)};
    const int bottom{std::clamp(static_cast<int>(centre.y + half.y), 0, image.rows)};
    if (right <= left || bottom <= top) {
        return {};
    }

    const cv::Mat crop{image(cv::Range{top, bottom}, cv::Range{left, right})};

    // The diagonal before rotating, so that no corner of the upright hand can
    // fall outside the frame once it is turned; the longer side otherwise.
    const int span{forRotation
                       ? static_cast<int>(std::hypot(static_cast<double>(crop.rows),
                                                     static_cast<double>(crop.cols)))
                       : std::max(crop.rows, crop.cols)};
    const int padHeight{std::max(span - crop.rows, 0)};
    const int padWidth{std::max(span - crop.cols, 0)};
    const int padLeft{padWidth / 2};
    const int padTop{padHeight / 2};

    cv::Mat padded;
    cv::copyMakeBorder(crop, padded, padTop, padHeight - padTop, padLeft, padWidth - padLeft,
                       cv::BORDER_CONSTANT, cv::Scalar{});

    return {padded,
            cv::Rect2f{static_cast<float>(left), static_cast<float>(top),
                       static_cast<float>(right - left), static_cast<float>(bottom - top)},
            cv::Point2f{static_cast<float>(left - padLeft), static_cast<float>(top - padTop)},
            true};
}

/// Grows a box about its centre and squares it off, which is the shape the palm
/// graph was trained on.
cv::Rect squaredRegion(const cv::Rect &region, float growth, const cv::Size &bounds)
{
    const float centreX{static_cast<float>(region.x) + static_cast<float>(region.width) / 2.0F};
    const float centreY{static_cast<float>(region.y) + static_cast<float>(region.height) / 2.0F};
    const float half{static_cast<float>(std::max(region.width, region.height)) * growth / 2.0F};

    const cv::Rect grown{cvRound(centreX - half), cvRound(centreY - half), cvRound(half * 2.0F),
                         cvRound(half * 2.0F)};
    return grown & cv::Rect{0, 0, bounds.width, bounds.height};
}

} // namespace

const std::vector<cv::Point2f> &palmAnchors()
{
    // Function local static: built once, and the C++ guarantee that the
    // initialization is thread safe matters here because the analysis worker
    // and the headless pipeline can both reach this from their own threads.
    static const std::vector<cv::Point2f> anchors{[]() {
        std::vector<cv::Point2f> generated;
        generated.reserve(2016);
        for (const auto &[stride, perCell] : {std::pair{8, 2}, std::pair{16, 6}}) {
            const int grid{HandLandmarker::palmInputSide / stride};
            for (int y{0}; y < grid; ++y) {
                for (int x{0}; x < grid; ++x) {
                    const auto centreX{static_cast<float>((x + 0.5) / grid)};
                    const auto centreY{static_cast<float>((y + 0.5) / grid)};
                    for (int repeat{0}; repeat < perCell; ++repeat) {
                        generated.emplace_back(centreX, centreY);
                    }
                }
            }
        }
        return generated;
    }()};
    return anchors;
}

QList<PalmDetection> decodePalms(const std::vector<cv::Mat> &outputs,
                                 const cv::Size &sourceSize,
                                 const cv::Point2f &padBias,
                                 float scoreThreshold,
                                 float nmsThreshold)
{
    // 18 trailing values are four box deltas and seven landmark pairs; 1 is the
    // score logit. Matched on that rather than on output order.
    const cv::Mat *regressors{nullptr};
    const cv::Mat *scores{nullptr};
    for (const cv::Mat &output : outputs) {
        if (output.dims != 3 || output.type() != CV_32F || !output.isContinuous()) {
            continue;
        }
        if (output.size[2] == 18) {
            regressors = &output;
        } else if (output.size[2] == 1) {
            scores = &output;
        }
    }
    if (regressors == nullptr || scores == nullptr) {
        return {};
    }

    const std::vector<cv::Point2f> &anchors{palmAnchors()};
    const int count{std::min({regressors->size[1], scores->size[1], static_cast<int>(anchors.size())})};
    if (count <= 0) {
        return {};
    }

    // After letterboxing, the content fills the input in whichever dimension is
    // longer, so anchor space maps back through the longer source side.
    const auto scale{static_cast<float>(std::max(sourceSize.width, sourceSize.height))};
    constexpr float inputSide{static_cast<float>(HandLandmarker::palmInputSide)};

    const auto *regression{reinterpret_cast<const float *>(regressors->data)};
    const auto *score{reinterpret_cast<const float *>(scores->data)};

    std::vector<cv::Rect2d> boxes;
    std::vector<float> confidences;
    std::vector<std::array<cv::Point2f, 7>> landmarks;
    boxes.reserve(static_cast<size_t>(count));
    confidences.reserve(static_cast<size_t>(count));
    landmarks.reserve(static_cast<size_t>(count));

    for (int index{0}; index < count; ++index) {
        const float confidence{1.0F / (1.0F + std::exp(-score[index]))};
        if (confidence < scoreThreshold) {
            continue;
        }

        const float *row{regression + static_cast<ptrdiff_t>(index) * 18};
        const cv::Point2f anchor{anchors[static_cast<size_t>(index)]};

        const float centreX{row[0] / inputSide + anchor.x};
        const float centreY{row[1] / inputSide + anchor.y};
        const float width{row[2] / inputSide};
        const float height{row[3] / inputSide};

        const double x{static_cast<double>((centreX - width / 2.0F) * scale - padBias.x)};
        const double y{static_cast<double>((centreY - height / 2.0F) * scale - padBias.y)};

        std::array<cv::Point2f, 7> palm{};
        for (int point{0}; point < 7; ++point) {
            palm[static_cast<size_t>(point)] =
                cv::Point2f{(row[4 + point * 2] / inputSide + anchor.x) * scale - padBias.x,
                            (row[5 + point * 2] / inputSide + anchor.y) * scale - padBias.y};
        }

        boxes.emplace_back(x, y, static_cast<double>(width * scale), static_cast<double>(height * scale));
        confidences.push_back(confidence);
        landmarks.push_back(palm);
    }

    if (boxes.empty()) {
        return {};
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, confidences, scoreThreshold, nmsThreshold, keep);

    QList<PalmDetection> detections;
    detections.reserve(static_cast<qsizetype>(keep.size()));
    for (const int index : keep) {
        const cv::Rect2d &box{boxes[static_cast<size_t>(index)]};
        detections.append(PalmDetection{cv::Rect2f{static_cast<float>(box.x), static_cast<float>(box.y),
                                                   static_cast<float>(box.width),
                                                   static_cast<float>(box.height)},
                                        landmarks[static_cast<size_t>(index)],
                                        confidences[static_cast<size_t>(index)]});
    }
    return detections;
}

double gripStrength(const std::array<cv::Point2f, handPointCount> &landmarks)
{
    constexpr std::array<std::pair<int, int>, 4> fingers{{{IndexTip, IndexMcp},
                                                         {MiddleTip, MiddleMcp},
                                                         {RingTip, RingMcp},
                                                         {PinkyTip, PinkyMcp}}};
    // Measured, not guessed. Across open hands the four ratios land between
    // 1.53 and 1.86, and across a closed fist between 0.74 and 0.80, so the
    // ends of the scale are put just inside those bands: a splayed hand reads
    // essentially zero and a fist saturates at one. Between them the measure is
    // linear, which is not physically exact but is monotone, and monotone is
    // all a threshold needs.
    constexpr double openRatio{1.65};
    constexpr double closedRatio{0.85};

    const cv::Point2f wrist{landmarks[HandWrist]};
    double total{0.0};
    int counted{0};
    for (const auto &[tip, knuckle] : fingers) {
        const cv::Point2f knuckleOffset{landmarks[static_cast<size_t>(knuckle)] - wrist};
        const double knuckleDistance{std::hypot(static_cast<double>(knuckleOffset.x),
                                                static_cast<double>(knuckleOffset.y))};
        if (knuckleDistance < 1e-3) {
            continue;
        }
        const cv::Point2f tipOffset{landmarks[static_cast<size_t>(tip)] - wrist};
        const double ratio{std::hypot(static_cast<double>(tipOffset.x),
                                      static_cast<double>(tipOffset.y))
                           / knuckleDistance};
        total += std::clamp((openRatio - ratio) / (openRatio - closedRatio), 0.0, 1.0);
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

bool HandLandmarker::load(const QString &palmModelPath, const QString &landmarkModelPath)
{
    m_loaded = false;
    if (palmModelPath.isEmpty() || landmarkModelPath.isEmpty()) {
        return false;
    }

    try {
        m_palmNet = cv::dnn::readNetFromONNX(palmModelPath.toStdString());
        m_handNet = cv::dnn::readNetFromONNX(landmarkModelPath.toStdString());
    } catch (const cv::Exception &error) {
        qCWarning(handLog) << "cannot read hand models" << error.what();
        return false;
    }
    if (m_palmNet.empty() || m_handNet.empty()) {
        qCWarning(handLog) << "empty network from" << palmModelPath << "or" << landmarkModelPath;
        return false;
    }

    for (cv::dnn::Net *net : {&m_palmNet, &m_handNet}) {
        net->setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net->setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
    m_loaded = true;
    return true;
}

std::optional<HandPose> HandLandmarker::detect(const cv::Mat &bgr, const cv::Rect &region)
{
    if (!m_loaded || bgr.empty() || region.empty()) {
        return std::nullopt;
    }

    const cv::Rect roi{squaredRegion(region, regionGrowth, bgr.size())};
    if (roi.width < minimumRegionSide || roi.height < minimumRegionSide) {
        return std::nullopt;
    }

    try {
        const cv::Mat crop{bgr(roi)};

        const Letterbox letterbox{letterboxForPalm(crop)};
        cv::Mat rgb;
        cv::cvtColor(letterbox.image, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

        m_palmNet.setInput(blobFromRgb(rgb));
        std::vector<cv::Mat> outputs;
        m_palmNet.forward(outputs, m_palmNet.getUnconnectedOutLayersNames());

        const QList<PalmDetection> palms{
            decodePalms(outputs, crop.size(), letterbox.padBias, m_palmScore, m_palmNms)};
        if (palms.isEmpty()) {
            return std::nullopt;
        }

        // NMSBoxes returns indices in descending score order, so the first is
        // the best. One hand per proposed region: the region came from one
        // wrist, and a second palm inside it belongs to somebody else's arm.
        std::optional<HandPose> pose{landmarksFor(crop, palms.constFirst())};
        if (!pose) {
            return std::nullopt;
        }

        pose->box.x += static_cast<float>(roi.x);
        pose->box.y += static_cast<float>(roi.y);
        for (cv::Point2f &point : pose->landmarks) {
            point.x += static_cast<float>(roi.x);
            point.y += static_cast<float>(roi.y);
        }
        return pose;
    } catch (const cv::Exception &error) {
        qCWarning(handLog) << "hand inference failed" << error.what();
        return std::nullopt;
    }
}

std::optional<HandPose> HandLandmarker::landmarksFor(const cv::Mat &bgrRoi, const PalmDetection &palm)
{
    // Stage one: a generous upright crop, big enough that rotating it in place
    // cannot lose a fingertip off an edge.
    const PalmCrop upright{cropAroundPalm(bgrRoi, palm.box, true)};
    if (!upright.valid) {
        return std::nullopt;
    }

    cv::Mat rgb;
    cv::cvtColor(upright.image, rgb, cv::COLOR_BGR2RGB);

    const cv::Point2f bias{upright.bias};
    const cv::Rect2f palmBox{upright.box.x - bias.x, upright.box.y - bias.y, upright.box.width,
                             upright.box.height};

    std::array<cv::Point2f, 7> points{};
    for (size_t index{0}; index < points.size(); ++index) {
        points[index] = palm.landmarks[index] - bias;
    }

    // Stage two: turn the hand upright. The model was trained on hands whose
    // fingers point up, so the angle from the palm base to the middle finger
    // base is rotated out before inference and back in afterwards.
    const cv::Point2f from{points[palmBaseIndex]};
    const cv::Point2f to{points[middleFingerBaseIndex]};
    double radians{std::numbers::pi / 2.0
                   - std::atan2(-static_cast<double>(to.y - from.y), static_cast<double>(to.x - from.x))};
    radians -= 2.0 * std::numbers::pi * std::floor((radians + std::numbers::pi) / (2.0 * std::numbers::pi));
    const double angle{radians * 180.0 / std::numbers::pi};

    const cv::Point2f centre{palmBox.x + palmBox.width / 2.0F, palmBox.y + palmBox.height / 2.0F};
    const cv::Mat rotation{cv::getRotationMatrix2D(centre, angle, 1.0)};
    cv::Mat rotated;
    cv::warpAffine(rgb, rotated, rotation, rgb.size());

    const auto *matrix{rotation.ptr<double>()};
    float minX{std::numeric_limits<float>::max()};
    float minY{std::numeric_limits<float>::max()};
    float maxX{std::numeric_limits<float>::lowest()};
    float maxY{std::numeric_limits<float>::lowest()};
    for (const cv::Point2f &point : points) {
        const auto x{static_cast<float>(matrix[0] * point.x + matrix[1] * point.y + matrix[2])};
        const auto y{static_cast<float>(matrix[3] * point.x + matrix[4] * point.y + matrix[5])};
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    const PalmCrop framed{cropAroundPalm(rotated, cv::Rect2f{minX, minY, maxX - minX, maxY - minY}, false)};
    if (!framed.valid) {
        return std::nullopt;
    }

    cv::Mat blob;
    cv::resize(framed.image, blob, cv::Size{handInputSide, handInputSide}, 0.0, 0.0, cv::INTER_AREA);
    blob.convertTo(blob, CV_32FC3, 1.0 / 255.0);

    m_handNet.setInput(blobFromRgb(blob));
    // By name, not by position. Two of this graph's four outputs are 21x3 and
    // two are scalars, so an enumeration order that shifted would swap screen
    // landmarks for metric ones silently. The names are fixed by the file, and
    // the file is pinned by sha256 in the manifest.
    std::vector<cv::Mat> outputs;
    m_handNet.forward(outputs, std::vector<cv::String>{"Identity", "Identity_1"});
    if (outputs.size() < 2 || outputs[0].total() < handPointCount * 3 || outputs[1].total() < 1) {
        return std::nullopt;
    }

    const auto confidence{static_cast<double>(reinterpret_cast<const float *>(outputs[1].data)[0])};
    if (confidence < static_cast<double>(m_handConfidence)) {
        return std::nullopt;
    }

    // Undo the 224 box, then the rotation, then the crop, in that order.
    const auto *raw{reinterpret_cast<const float *>(outputs[0].data)};
    const float scaleFactor{std::max(framed.box.width, framed.box.height)
                            / static_cast<float>(handInputSide)};
    const cv::Mat coordsRotation{cv::getRotationMatrix2D(cv::Point2f{0.0F, 0.0F}, angle, 1.0)};
    const auto *coords{coordsRotation.ptr<double>()};

    const double inverseX{-(matrix[0] * matrix[2] + matrix[3] * matrix[5])};
    const double inverseY{-(matrix[1] * matrix[2] + matrix[4] * matrix[5])};
    const cv::Point2f framedCentre{framed.box.x + framed.box.width / 2.0F,
                                   framed.box.y + framed.box.height / 2.0F};
    const auto originalX{static_cast<float>(matrix[0] * framedCentre.x + matrix[3] * framedCentre.y
                                            + inverseX)};
    const auto originalY{static_cast<float>(matrix[1] * framedCentre.x + matrix[4] * framedCentre.y
                                            + inverseY)};

    HandPose pose;
    pose.confidence = confidence;

    float boxMinX{std::numeric_limits<float>::max()};
    float boxMinY{std::numeric_limits<float>::max()};
    float boxMaxX{std::numeric_limits<float>::lowest()};
    float boxMaxY{std::numeric_limits<float>::lowest()};
    for (int point{0}; point < handPointCount; ++point) {
        const float localX{(raw[point * 3] - static_cast<float>(handInputSide) / 2.0F) * scaleFactor};
        const float localY{(raw[point * 3 + 1] - static_cast<float>(handInputSide) / 2.0F) * scaleFactor};
        const auto x{static_cast<float>(coords[0] * localX + coords[3] * localY) + originalX + bias.x};
        const auto y{static_cast<float>(coords[1] * localX + coords[4] * localY) + originalY + bias.y};
        pose.landmarks[static_cast<size_t>(point)] = cv::Point2f{x, y};
        boxMinX = std::min(boxMinX, x);
        boxMinY = std::min(boxMinY, y);
        boxMaxX = std::max(boxMaxX, x);
        boxMaxY = std::max(boxMaxY, y);
    }

    const float width{boxMaxX - boxMinX};
    const float height{boxMaxY - boxMinY};
    const cv::Point2f boxCentre{boxMinX + width / 2.0F,
                                boxMinY + height / 2.0F + handBoxShiftY * height};
    pose.box = cv::Rect2f{boxCentre.x - width * handBoxEnlarge / 2.0F,
                          boxCentre.y - height * handBoxEnlarge / 2.0F, width * handBoxEnlarge,
                          height * handBoxEnlarge};
    pose.grip = gripStrength(pose.landmarks);
    return pose;
}

} // namespace Kerfur
