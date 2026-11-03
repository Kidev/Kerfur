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

/// A hand shape worth naming.
///
/// Deliberately short. Every entry here is a shape a person makes on purpose
/// and would be surprised not to have noticed, which is the only useful test:
/// a classifier that reports thirty poses reports twenty nine accidents. The
/// set is also chosen to be unambiguous under reflection, because the frame
/// Kerfur analyses is a mirror -- a thumb pointing up still points up in it,
/// which is not true of anything that depends on which hand is which.
enum class HandGesture {
    None,
    OpenPalm,
    Fist,
    ThumbUp,
    ThumbDown,
    Point,
    Victory,
    Ok,
};

/// The name a view shows, or an empty string for `None`.
QString gestureName(HandGesture gesture);

/// One hand, measured rather than extrapolated.
struct HandPose
{
    /// Pixels, in the coordinates of the frame that was passed in.
    cv::Rect2f box;
    std::array<cv::Point2f, handPointCount> landmarks{};
    double confidence{0.0};
    /// Zero for a flat open hand, one for a closed fist. See gripStrength.
    double grip{0.0};
    /// The shape, if it is one of the named ones. See handGesture.
    HandGesture gesture{HandGesture::None};
    /// Zero to five digits held out. See extendedFingerCount.
    int fingers{0};
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
    ///
    /// The proposal already holds a whole spread hand, so this is margin for it
    /// being in slightly the wrong place rather than for it being too small.
    /// Growing it further would be worse, not safer: the crop is letterboxed
    /// into 192 pixels, and every bit of forearm and wall added to it is taken
    /// off the palm the detector is looking for.
    static constexpr float regionGrowth{1.9F};
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

/// How far a finger is extended, as the same wrist-relative ratio gripStrength
/// averages: about 1.7 straight out, about 0.8 curled into the palm.
///
/// Exposed because the gesture rules below need the four fingers separately and
/// the grip needs them averaged, and computing the ratio twice in two places is
/// how the two would drift apart.
double fingerExtension(const std::array<cv::Point2f, handPointCount> &landmarks, HandPoint tip);

/// True when the thumb is held clear of the palm rather than folded across it.
///
/// Measured against the pinky knuckle rather than the wrist: the thumb's own
/// distance from the wrist hardly changes when it folds, which is exactly why
/// gripStrength excludes it, but the distance across the palm nearly doubles.
bool thumbIsOut(const std::array<cv::Point2f, handPointCount> &landmarks);

/// How many digits are held out, thumb included, from zero to five.
///
/// The counting reading of a hand, and the one thing here that always answers.
/// handGesture refuses a hand whose fingers are between the two bands, because
/// naming a shape wrongly means acting wrongly; this is only ever displayed, so
/// each digit is decided at a single line drawn halfway between those bands and
/// a hand held up to say three reads as three whether or not three is a shape
/// with a name.
///
/// The thumb is judged by thumbIsOut rather than by the wrist ratio the four
/// fingers use, for the reason gripStrength gives: it folds across the palm
/// instead of towards the wrist, so its own ratio barely moves. That is what
/// makes a thumbs up one and a fist zero.
int extendedFingerCount(const std::array<cv::Point2f, handPointCount> &landmarks);

/// Which of the named shapes this hand is making, or `None`.
///
/// Every rule is a ratio between two lengths of the same hand, so the answer
/// does not change with how near the person is or which way the hand is turned.
/// The one exception is the thumb pair, which has to consult image up and down
/// because that is what makes a thumb up mean approval and a thumb down mean
/// the opposite; they are the only two entries that know which way is up.
HandGesture handGesture(const std::array<cv::Point2f, handPointCount> &landmarks);

} // namespace Kerfur

#endif // KERFUR_HANDLANDMARKER_H
