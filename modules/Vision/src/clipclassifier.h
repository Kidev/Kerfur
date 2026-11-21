// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CLIPCLASSIFIER_H
#define KERFUR_CLIPCLASSIFIER_H

#include "accelerator.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace Kerfur {

/// One set of zero-shot answers: a label per row of unit text embeddings. An
/// empty label is the answer that names nothing, such as an empty hand.
/// Several rows may share a label: synonyms, whose probabilities add up.
struct ClipSet
{
    QStringList labels;
    /// One row per label, `CV_32F`, each of unit length.
    cv::Mat embeddings;
};

/// What `assets/clip/<encoder>.json` holds: the text side of CLIP, computed
/// once in full precision by cmake/clip-labels.py.
struct ClipLabels
{
    /// The file name of the image encoder the embeddings were made for.
    QString encoder;
    int dimension{0};
    float logitScale{100.0F};
    QHash<QString, ClipSet> sets;
};

/// Reads a labels document, or returns nothing and says why in `problem`.
std::optional<ClipLabels> parseClipLabels(const QByteArray &json, QString &problem);

/// The best answer of `set` for one image embedding: the softmax over `scale`
/// times the cosine similarities, as CLIP scores zero-shot, summed over the
/// rows that share a label.
struct ClipAnswer
{
    /// Empty when the answer that names nothing won.
    QString label;
    float probability{0.0F};
};
ClipAnswer zeroShot(const cv::Mat &imageEmbedding, const ClipSet &set, float scale);

/// The CLIP image encoder on the accelerator, and the labels it is read
/// against. One encoder serves every set: held objects and expressions are
/// two vocabularies over the same picture space.
class ClipClassifier
{
public:
    /// Opens the compiled encoder and reads the labels made for it. Refuses
    /// labels made for another encoder, whose embeddings would be noise.
    bool load(Accel::Device &device, const QString &encoderPath, const QString &labelsPath);
    bool isLoaded() const { return m_model != nullptr; }
    bool hasSet(const QString &set) const { return m_labels.sets.contains(set); }

    /// Answers `set` for the square around `region` of `bgr`. The region is
    /// padded to a square rather than stretched, since CLIP saw undistorted
    /// pictures, and anything of it outside the frame is black.
    std::optional<ClipAnswer> classify(const cv::Mat &bgr,
                                       const cv::Rect &region,
                                       const QString &set);

    /// The encoder's square input side.
    static constexpr int inputSide{224};

private:
    std::unique_ptr<Accel::Model> m_model;
    std::vector<Accel::Tensor> m_outputs;
    ClipLabels m_labels;
    bool m_reportedFailure{false};
};

/// The square of side `max(region.width, region.height)` centred on `region`,
/// cut from `bgr` with black where it leaves the frame.
cv::Mat squareCrop(const cv::Mat &bgr, const cv::Rect &region);

} // namespace Kerfur

#endif // KERFUR_CLIPCLASSIFIER_H
