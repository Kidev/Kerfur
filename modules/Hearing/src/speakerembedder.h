// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SPEAKEREMBEDDER_H
#define KERFUR_SPEAKEREMBEDDER_H

#include "filterbank.h"

#include <QString>

#include <opencv2/dnn.hpp>

#include <vector>

namespace Kerfur {

/// Turns a spoken utterance into a vector that is close to other utterances by
/// the same person and far from everyone else's.
///
/// The network is WeSpeaker's ResNet34, and it is run in two pieces. OpenCV's
/// DNN module has no ReduceProd, and the exported graph uses one to build the
/// `n - 1` denominator of the unbiased variance inside its statistics pooling
/// layer; the whole tail therefore fails to execute, and it fails at run time
/// rather than at load. What is done instead is to run the convolutional trunk,
/// which OpenCV executes correctly and which is all of the cost, and then to
/// carry out the pooling and the final projection here, reading the projection
/// weights straight out of the same graph. It is not a reimplementation of the
/// model: it is the last four operations of it, in a place that can run them.
class SpeakerEmbedder
{
public:
    bool load(const QString &modelPath);
    bool isLoaded() const { return m_loaded; }

    /// 16 kHz mono samples in [-1, 1]. Returns an L2 normalized row vector, or
    /// an empty matrix when the utterance is too short or the model is not
    /// loaded.
    cv::Mat embed(const std::vector<float> &samples);

    static double cosineSimilarity(const cv::Mat &left, const cv::Mat &right);

    /// Below this an embedding says more about the room than about the speaker.
    /// Measured rather than assumed: against a gallery built from one voice,
    /// two second windows of held out speech by the same person scored 0.65 to
    /// 0.83 while the nearest of twenty eight other speakers reached 0.19,
    /// where one second windows fell as low as 0.07 and overlapped the
    /// impostors completely.
    static constexpr int minimumSeconds{2};

    /// Longer utterances are truncated. The statistics pooling averages over
    /// time, so past a few seconds a longer segment costs proportionally more
    /// and adds almost nothing.
    static constexpr int maximumSeconds{12};

    static constexpr int sampleRate{16000};

private:
    cv::dnn::Net m_net;
    FilterBank m_frontend{sampleRate, 80};

    /// The last layer before the pooling that OpenCV cannot run. Found by
    /// walking to the first Reduce rather than by name, so a re-export with
    /// different node names still works and a differently shaped graph is
    /// refused instead of silently mis-pooled.
    std::string m_trunkOutput;

    cv::Mat m_projection;  ///< `embedding x pooled`
    cv::Mat m_bias;        ///< `embedding x 1`
    cv::Mat m_centre;      ///< subtracted after the projection
    bool m_loaded{false};
};

} // namespace Kerfur

#endif // KERFUR_SPEAKEREMBEDDER_H
