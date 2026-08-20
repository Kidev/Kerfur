// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "speakerembedder.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(speakerLog, "kerfur.hearing.speaker")

// The name the exported graph gives its input. Fixed by the file, which the
// manifest pins by checksum.
constexpr const char *inputName{"feats"};

// The epsilon the graph adds inside its square root, read off the constant
// node it was exported with. It exists to keep the derivative finite and is
// far below any real variance, but it is part of what the trained weights saw.
constexpr float varianceEpsilon{1e-8F};

} // namespace

bool SpeakerEmbedder::load(const QString &modelPath)
{
    m_loaded = false;
    m_trunkOutput.clear();
    if (modelPath.isEmpty()) {
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());
    } catch (const cv::Exception &error) {
        qCWarning(speakerLog) << "cannot read speaker model" << modelPath << error.what();
        return false;
    }
    if (m_net.empty()) {
        return false;
    }
    m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

    // Where the trunk ends: the layer before the first Reduce, which is the
    // first operation of the pooling. Found by walking rather than by name, so
    // that a re-export with different node names still works and a graph of an
    // unexpected shape is refused rather than silently mis-pooled.
    const std::vector<cv::String> names{m_net.getLayerNames()};
    std::string previous;
    for (const cv::String &name : names) {
        if (m_net.getLayer(name)->type == "Reduce") {
            m_trunkOutput = previous;
            break;
        }
        previous = name;
    }

    // The projection and the centre sit after the pooling, so they are found in
    // a second pass. Identified by shape rather than by node name: the
    // projection is the only two dimensional weight matrix in the graph, and
    // the centre the only constant as long as its output.
    for (const cv::String &name : names) {
        const cv::Ptr<cv::dnn::Layer> layer{m_net.getLayer(name)};
        if (layer->blobs.empty()) {
            continue;
        }
        if (layer->type == "Gemm" && layer->blobs.size() == 2 && layer->blobs[0].dims == 2) {
            m_projection = layer->blobs[0].clone();
            m_bias = layer->blobs[1].clone().reshape(1, static_cast<int>(layer->blobs[1].total()));
        } else if (layer->type == "Const" && layer->blobs.size() == 1
                   && layer->blobs[0].total() > 1) {
            const cv::Mat &blob{layer->blobs[0]};
            if (!m_projection.empty()
                && static_cast<int>(blob.total()) == m_projection.rows) {
                m_centre = blob.clone().reshape(1, 1);
            }
        }
    }

    if (m_trunkOutput.empty() || m_projection.empty() || m_bias.empty()) {
        qCWarning(speakerLog) << "speaker model has an unexpected shape; voices stay unknown";
        return false;
    }
    if (m_centre.empty()) {
        // Optional: an export without the centring constant is still usable,
        // it simply produces embeddings offset by a constant, which cosine
        // similarity is not invariant to but which affects every gallery entry
        // and every query equally.
        m_centre = cv::Mat::zeros(1, m_projection.rows, CV_32F);
    }

    m_loaded = true;
    qCInfo(speakerLog) << "speaker model trunk ends at" << QString::fromStdString(m_trunkOutput)
                       << "projection" << m_projection.rows << "x" << m_projection.cols;
    return true;
}

cv::Mat SpeakerEmbedder::embed(const std::vector<float> &samples)
{
    if (!m_loaded) {
        return {};
    }

    const size_t wanted{static_cast<size_t>(maximumSeconds) * sampleRate};
    const std::vector<float> trimmed{samples.size() > wanted
                                         ? std::vector<float>{samples.cbegin(),
                                                              samples.cbegin()
                                                                  + static_cast<qsizetype>(wanted)}
                                         : samples};
    if (trimmed.size() < static_cast<size_t>(minimumSeconds) * sampleRate) {
        return {};
    }

    const cv::Mat features{m_frontend.compute(trimmed)};
    if (features.empty()) {
        return {};
    }

    try {
        // The graph wants `1 x frames x bins`, which is the feature matrix with
        // a batch dimension in front of it and no copy of the data.
        const int shape[3]{1, features.rows, features.cols};
        const cv::Mat input{3, shape, CV_32F, const_cast<float *>(features.ptr<float>())};

        m_net.setInput(input, inputName);
        const cv::Mat trunk{m_net.forward(m_trunkOutput)};
        if (trunk.dims != 4) {
            qCWarning(speakerLog) << "trunk produced" << trunk.dims << "dimensions, expected 4";
            return {};
        }

        const int channels{trunk.size[1]};
        const int bands{trunk.size[2]};
        const int steps{trunk.size[3]};
        if (steps < 2) {
            return {};
        }

        // Temporal statistics pooling: the mean and the standard deviation of
        // every channel and band over time, concatenated. The variance is the
        // unbiased one, over `steps - 1`, because that is what torch.std
        // computes and what the projection was trained against.
        const int pooled{channels * bands};
        cv::Mat statistics{cv::Mat::zeros(1, pooled * 2, CV_32F)};
        float *const means{statistics.ptr<float>()};
        float *const deviations{means + pooled};

        const float *const data{trunk.ptr<float>()};
        for (int channel{0}; channel < channels; ++channel) {
            for (int band{0}; band < bands; ++band) {
                const float *series{data
                                    + ((static_cast<size_t>(channel) * bands + band) * steps)};
                double total{0.0};
                for (int step{0}; step < steps; ++step) {
                    total += series[step];
                }
                const double mean{total / steps};

                double squared{0.0};
                for (int step{0}; step < steps; ++step) {
                    const double difference{series[step] - mean};
                    squared += difference * difference;
                }
                const double variance{squared / (steps - 1)};

                // The flattening is over channel then band, which is the order
                // the graph's own Flatten produces and therefore the order the
                // projection's columns are in.
                const int index{(channel * bands) + band};
                means[index] = static_cast<float>(mean);
                deviations[index] = static_cast<float>(std::sqrt(variance + varianceEpsilon));
            }
        }

        if (statistics.cols != m_projection.cols) {
            qCWarning(speakerLog) << "pooled" << statistics.cols << "values but the projection wants"
                                  << m_projection.cols;
            return {};
        }

        cv::Mat embedding{statistics * m_projection.t()};
        embedding += m_bias.reshape(1, 1);
        embedding -= m_centre;

        const double norm{cv::norm(embedding)};
        if (norm <= 0.0) {
            return {};
        }
        // Stored normalized so that a cosine is a dot product, and so that a
        // gallery written by one version is comparable with a query from
        // another.
        embedding /= norm;
        return embedding;
    } catch (const cv::Exception &error) {
        qCWarning(speakerLog) << "speaker inference failed" << error.what();
        return {};
    }
}

double SpeakerEmbedder::cosineSimilarity(const cv::Mat &left, const cv::Mat &right)
{
    if (left.empty() || right.empty() || left.total() != right.total()) {
        return -1.0;
    }
    const cv::Mat a{left.reshape(1, 1)};
    const cv::Mat b{right.reshape(1, 1)};
    const double denominator{cv::norm(a) * cv::norm(b)};
    if (denominator <= 0.0) {
        return -1.0;
    }
    return a.dot(b) / denominator;
}

} // namespace Kerfur
