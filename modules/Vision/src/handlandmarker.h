// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_HANDLANDMARKER_H
#define KERFUR_HANDLANDMARKER_H

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <opencv2/dnn.hpp>

#include <array>
#include <optional>
#include <vector>

namespace Kerfur {

/// The twenty-one MediaPipe hand landmarks, in the order the model emits them.
///
/// Named because the grip measurement below indexes four of them by meaning and
/// a bare 8 in that arithmetic is unreadable.
enum HandPoint {
    HandWrist = 0,
    ThumbCmc,
    ThumbMcp,
    ThumbIp,
    ThumbTip,
    IndexMcp,
    IndexPip,
    IndexDip,
    IndexTip,
    MiddleMcp,
    MiddlePip,
    MiddleDip,
    MiddleTip,
    RingMcp,
    RingPip,
    RingDip,
    RingTip,
    PinkyMcp,
    PinkyPip,
    PinkyDip,
    PinkyTip,
    handPointCount
};

/// One palm the detector found, in the pixel coordinates of the image it was
/// given. The seven landmarks are the palm's own, not the hand's: five finger
/// bases and two wrist points, which is all the second stage needs to work out
/// which way the hand is pointing.
struct PalmDetection
{
    cv::Rect2f box;
    std::array<cv::Point2f, 7> landmarks{};
    float score{0.0F};
};

/// One hand, measured rather than extrapolated.
struct HandPose
{
    /// Pixels, in the coordinates of the frame that was passed in.
    cv::Rect2f box;
    std::array<cv::Point2f, handPointCount> landmarks{};
    double confidence{0.0};
    /// Zero for a flat open hand, one for a closed fist. See gripStrength.
    double grip{0.0};
};

/// MediaPipe's two stage hand pipeline: palm detection, then landmarks.
///
/// The models are the ONNX exports from OpenCV Zoo, so they run on the cv::dnn
/// that is already linked rather than dragging in MediaPipe's Bazel build, and
/// they are pinned in assets/models.json like every other stage.
///
/// One departure from the upstream pipeline is deliberate. MediaPipe runs the
/// palm detector over the whole frame and then tracks. Here it is given a
/// region, because the pose stage has already found a wrist and, more to the
/// point, has already established whose wrist it is. Searching the whole frame
/// would find the hands of strangers too, which is precisely what the identity
/// gate upstream exists to prevent, and it would cost a full 192x192 pass to
/// produce answers that would then be thrown away.
class HandLandmarker
{
public:
    bool load(const QString &palmModelPath, const QString &landmarkModelPath);
    bool isLoaded() const { return m_loaded; }

    /// The single best hand inside `region` of `bgr`, or nothing.
    ///
    /// `region` is a coarse proposal and is enlarged before the search: what
    /// the skeleton calls a hand is a guess from a forearm direction, and the
    /// palm is regularly a little outside it.
    std::optional<HandPose> detect(const cv::Mat &bgr, const cv::Rect &region);

    void setPalmScoreThreshold(float threshold) { m_palmScore = threshold; }
    void setHandConfidenceThreshold(float threshold) { m_handConfidence = threshold; }

    /// The two graphs' fixed input sides.
    static constexpr int palmInputSide{192};
    static constexpr int handInputSide{224};
    /// How much the proposed region is grown before the palm search.
    static constexpr float regionGrowth{2.2F};
    /// Below this many pixels a side the crop carries no palm worth finding.
    static constexpr int minimumRegionSide{24};

private:
    std::optional<HandPose> landmarksFor(const cv::Mat &bgrRoi, const PalmDetection &palm);

    cv::dnn::Net m_palmNet;
    cv::dnn::Net m_handNet;
    bool m_loaded{false};
    float m_palmScore{0.55F};
    float m_palmNms{0.3F};
    float m_handConfidence{0.5F};
};

/// The 2016 anchor centres of the 192x192 palm graph.
///
/// Upstream ships these as a literal table of 2016 pairs. They are just the
/// standard SSD grid - a 24x24 layer with two anchors per cell followed by a
/// 12x12 layer with six - so they are generated here instead. Regenerating
/// rather than transcribing removes two thousand lines in which a single typo
/// would be invisible; tst_handlandmarker checks the result against the
/// published table's first and last entries.
const std::vector<cv::Point2f> &palmAnchors();

/// Turns the palm graph's two tensors into detections in source pixels.
///
/// Outputs are matched by their trailing dimension - 18 for the box and palm
/// landmark deltas, 1 for the score - and not by name or position, for the same
/// reason decodeNanoDet does it: which order an ONNX export lists them in is
/// not a promise anybody made.
QList<PalmDetection> decodePalms(const std::vector<cv::Mat> &outputs,
                                 const cv::Size &sourceSize,
                                 const cv::Point2f &padBias,
                                 float scoreThreshold,
                                 float nmsThreshold);

/// How closed a hand is, from zero (flat and open) to one (fist).
///
/// Per finger, the tip's distance from the wrist is compared with the knuckle's
/// distance from the wrist. An extended finger puts its tip roughly twice as
/// far out as its knuckle; a curled one brings the tip back level with it. The
/// ratio is taken rather than the distance so that the measure survives the
/// hand being near, far, or at any angle, which a raw pixel threshold does not.
///
/// The thumb is excluded. It folds across the palm rather than towards the
/// wrist, so its ratio barely moves between an open hand and a fist and
/// including it only dilutes the other four.
double gripStrength(const std::array<cv::Point2f, handPointCount> &landmarks);

} // namespace Kerfur

#endif // KERFUR_HANDLANDMARKER_H
