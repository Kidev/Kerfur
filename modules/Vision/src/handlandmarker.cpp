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

/// The four fingers every shape rule counts, in the order they are counted.
/// The thumb is not among them: it moves across the palm rather than away from
/// the wrist, so it is measured its own way and asked its own questions.
constexpr std::array<HandPoint, 4> fingerTips{IndexTip, MiddleTip, RingTip, PinkyTip};

/// A fingertip's own knuckle. The landmark order runs base to tip within each
/// digit, so this is arithmetic rather than a table, but the table is written
/// out because a reader should not have to trust that.
constexpr HandPoint knuckleFor(HandPoint tip)
{
    switch (tip) {
    case ThumbTip:
        return ThumbMcp;
    case IndexTip:
        return IndexMcp;
    case MiddleTip:
        return MiddleMcp;
    case RingTip:
        return RingMcp;
    case PinkyTip:
        return PinkyMcp;
    default:
        break;
    }
    return handPointCount;
}

double distance(const cv::Point2f &from, const cv::Point2f &to)
{
    return std::hypot(static_cast<double>(from.x - to.x), static_cast<double>(from.y - to.y));
}

/// Where a finger stops being ambiguous. The two bands are the ones gripStrength
/// was calibrated against -- open fingers at 1.53 and above, curled ones at 0.80
/// and below -- pulled in far enough that a finger between the two is refused
/// rather than rounded to whichever it is nearer.
constexpr double extendedRatio{1.35};
constexpr double curledRatio{1.15};

/// The single line the counting reading uses, halfway between the two bands
/// above. Counting cannot abstain -- a hand held up to say three has to come
/// back as a number -- so the refusal band is collapsed to a point and the
/// worst a marginal finger can do is be counted or not, rather than throw the
/// whole hand away.
constexpr double countingRatio{(extendedRatio + curledRatio) / 2.0};

/// How much further from the pinky knuckle an extended thumb sits than its own
/// knuckle does. A thumb folded across the palm comes in around 0.5 and one held
/// clear around 1.5, so the line is drawn where nothing lands.
constexpr double thumbOutRatio{1.15};

/// How close the thumb and index tips have to be, as a fraction of the palm's
/// length, to count as touching.
constexpr double okTouchFraction{0.4};

/// How much of the thumb's length has to point up or down for it to be a thumb
/// up or a thumb down: this is the cosine of about 57 degrees off vertical, so a
/// thumb held out sideways is a fist with an opinion and reads as neither.
constexpr double thumbVerticalFraction{0.55};

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

double fingerExtension(const std::array<cv::Point2f, handPointCount> &landmarks, HandPoint tip)
{
    const HandPoint knuckle{knuckleFor(tip)};
    if (knuckle == handPointCount) {
        return 0.0;
    }

    const cv::Point2f wrist{landmarks[HandWrist]};
    const double knuckleDistance{distance(landmarks[static_cast<size_t>(knuckle)], wrist)};
    if (knuckleDistance < 1e-3) {
        return 0.0;
    }
    return distance(landmarks[static_cast<size_t>(tip)], wrist) / knuckleDistance;
}

bool thumbIsOut(const std::array<cv::Point2f, handPointCount> &landmarks)
{
    const cv::Point2f across{landmarks[PinkyMcp]};
    const double folded{distance(landmarks[ThumbMcp], across)};
    if (folded < 1e-3) {
        return false;
    }
    return distance(landmarks[ThumbTip], across) / folded > thumbOutRatio;
}

double gripStrength(const std::array<cv::Point2f, handPointCount> &landmarks)
{
    // Measured, not guessed. Across open hands the four ratios land between
    // 1.53 and 1.86, and across a closed fist between 0.74 and 0.80, so the
    // ends of the scale are put just inside those bands: a splayed hand reads
    // essentially zero and a fist saturates at one. Between them the measure is
    // linear, which is not physically exact but is monotone, and monotone is
    // all a threshold needs.
    constexpr double openRatio{1.65};
    constexpr double closedRatio{0.85};

    double total{0.0};
    int counted{0};
    for (const HandPoint tip : fingerTips) {
        const double ratio{fingerExtension(landmarks, tip)};
        if (ratio <= 0.0) {
            continue;
        }
        total += std::clamp((openRatio - ratio) / (openRatio - closedRatio), 0.0, 1.0);
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

int extendedFingerCount(const std::array<cv::Point2f, handPointCount> &landmarks)
{
    int count{thumbIsOut(landmarks) ? 1 : 0};
    for (const HandPoint tip : fingerTips) {
        if (fingerExtension(landmarks, tip) >= countingRatio) {
            ++count;
        }
    }
    return count;
}

QString gestureName(HandGesture gesture)
{
    switch (gesture) {
    case HandGesture::OpenPalm:
        return QStringLiteral("open palm");
    case HandGesture::Fist:
        return QStringLiteral("fist");
    case HandGesture::ThumbUp:
        return QStringLiteral("thumbs up");
    case HandGesture::ThumbDown:
        return QStringLiteral("thumbs down");
    case HandGesture::Point:
        return QStringLiteral("pointing");
    case HandGesture::Victory:
        return QStringLiteral("victory");
    case HandGesture::Ok:
        return QStringLiteral("ok");
    case HandGesture::None:
        break;
    }
    return {};
}

HandGesture handGesture(const std::array<cv::Point2f, handPointCount> &landmarks)
{
    // The palm, as the length everything else is measured against. A hand this
    // small in the frame has landmarks that are mostly quantisation, and the
    // rules below would be reading noise.
    const double palm{distance(landmarks[MiddleMcp], landmarks[HandWrist])};
    if (palm < 1e-3) {
        return HandGesture::None;
    }

    // The ring first, because it is the one shape whose index finger is neither
    // out nor curled but drawn round to meet the thumb, and the strict test
    // below would throw the whole hand away for it. What makes it an O is the
    // two tips touching while the other three fingers stand clear, and that is
    // what is asked.
    const bool touching{distance(landmarks[ThumbTip], landmarks[IndexTip]) < palm * okTouchFraction};
    const bool trailingOut{fingerExtension(landmarks, MiddleTip) >= extendedRatio
                           && fingerExtension(landmarks, RingTip) >= extendedRatio
                           && fingerExtension(landmarks, PinkyTip) >= extendedRatio};
    if (touching && trailingOut) {
        return HandGesture::Ok;
    }

    // Every finger has to be decidedly one thing or the other. A hand caught
    // halfway between two shapes is not a third shape, and reporting one is
    // worse than reporting nothing: the gesture is about to be acted on.
    std::array<bool, fingerTips.size()> out{};
    for (size_t index{0}; index < fingerTips.size(); ++index) {
        const double ratio{fingerExtension(landmarks, fingerTips[index])};
        if (ratio >= extendedRatio) {
            out[index] = true;
        } else if (ratio > curledRatio) {
            return HandGesture::None;
        }
    }
    const int raised{static_cast<int>(std::count(out.cbegin(), out.cend(), true))};

    if (raised == static_cast<int>(fingerTips.size())) {
        return HandGesture::OpenPalm;
    }
    if (raised == 0) {
        if (!thumbIsOut(landmarks)) {
            return HandGesture::Fist;
        }
        // Which way the thumb points, and only here. The frame is a mirror, so
        // left and right are not the subject's; up and down survive a
        // reflection, which is the whole reason this pair is in the set and a
        // sideways thumb is not.
        const cv::Point2f thumb{landmarks[ThumbTip] - landmarks[ThumbMcp]};
        const double length{std::hypot(static_cast<double>(thumb.x), static_cast<double>(thumb.y))};
        if (length < 1e-3) {
            return HandGesture::Fist;
        }
        const double vertical{static_cast<double>(thumb.y) / length};
        if (vertical <= -thumbVerticalFraction) {
            return HandGesture::ThumbUp;
        }
        if (vertical >= thumbVerticalFraction) {
            return HandGesture::ThumbDown;
        }
        return HandGesture::Fist;
    }
    if (raised == 1 && out[0]) {
        return HandGesture::Point;
    }
    if (raised == 2 && out[0] && out[1]) {
        return HandGesture::Victory;
    }
    return HandGesture::None;
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
    pose.gesture = handGesture(pose.landmarks);
    pose.fingers = extendedFingerCount(pose.landmarks);
    return pose;
}

} // namespace Kerfur
