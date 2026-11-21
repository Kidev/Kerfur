// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "clipclassifier.h"

#include "acceleratordecode.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(clipLog, "kerfur.vision.clip")

} // namespace

std::optional<ClipLabels> parseClipLabels(const QByteArray &json, QString &problem)
{
    QJsonParseError error{};
    const QJsonDocument document{QJsonDocument::fromJson(json, &error)};
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        problem = QStringLiteral("not a JSON object: %1").arg(error.errorString());
        return std::nullopt;
    }
    const QJsonObject root{document.object()};

    ClipLabels labels;
    labels.encoder = root.value(QStringLiteral("encoder")).toString();
    labels.dimension = root.value(QStringLiteral("dimension")).toInt();
    const double scale{root.value(QStringLiteral("logitScale")).toDouble(100.0)};
    labels.logitScale = static_cast<float>(scale);
    if (labels.encoder.isEmpty() || labels.dimension <= 0) {
        problem = QStringLiteral("no encoder or dimension");
        return std::nullopt;
    }
    const QString type{root.value(QStringLiteral("dtype")).toString(QStringLiteral("float32"))};
    const bool half{type == QStringLiteral("float16")};
    if (!half && type != QStringLiteral("float32")) {
        problem = QStringLiteral("embeddings stored as %1").arg(type);
        return std::nullopt;
    }

    const QJsonObject sets{root.value(QStringLiteral("sets")).toObject()};
    for (auto entry{sets.constBegin()}; entry != sets.constEnd(); ++entry) {
        const QJsonObject set{entry.value().toObject()};
        ClipSet parsed;
        for (const QJsonValue &label : set.value(QStringLiteral("labels")).toArray()) {
            parsed.labels.append(label.toString());
        }
        const QByteArray packed{
            QByteArray::fromBase64(set.value(QStringLiteral("embeddings")).toString().toLatin1())};
        const auto rows{static_cast<int>(parsed.labels.size())};
        const auto expected{static_cast<qsizetype>(rows) * labels.dimension *
                            static_cast<qsizetype>(half ? sizeof(quint16) : sizeof(float))};
        if (rows == 0 || packed.size() != expected) {
            problem = QStringLiteral("set %1 holds %2 bytes for %3 labels of %4 values")
                          .arg(entry.key())
                          .arg(packed.size())
                          .arg(rows)
                          .arg(labels.dimension);
            return std::nullopt;
        }
        // Little endian, which is every platform Kerfur builds for.
        cv::Mat stored(rows, labels.dimension, half ? CV_16F : CV_32F);
        std::memcpy(stored.data, packed.constData(), static_cast<size_t>(packed.size()));
        stored.convertTo(parsed.embeddings, CV_32F);
        labels.sets.insert(entry.key(), parsed);
    }
    if (labels.sets.isEmpty()) {
        problem = QStringLiteral("no label sets");
        return std::nullopt;
    }
    return labels;
}

ClipAnswer zeroShot(const cv::Mat &imageEmbedding, const ClipSet &set, float scale)
{
    if (imageEmbedding.empty() || set.embeddings.empty() ||
        static_cast<int>(imageEmbedding.total()) != set.embeddings.cols) {
        return {};
    }
    const cv::Mat image{imageEmbedding.reshape(1, 1)};
    const double norm{cv::norm(image)};
    if (norm <= 0.0) {
        return {};
    }

    cv::Mat similarities;
    cv::gemm(set.embeddings, image, scale / norm, cv::noArray(), 0.0, similarities, cv::GEMM_2_T);
    double largest{0.0};
    cv::minMaxLoc(similarities, nullptr, &largest);

    // Softmax over the rows, then summed by label, so that synonyms count as
    // one answer instead of splitting it.
    QHash<QString, double> byLabel;
    double total{0.0};
    for (int row{0}; row < similarities.rows; ++row) {
        const double weight{
            std::exp(static_cast<double>(similarities.at<float>(row, 0)) - largest)};
        byLabel[set.labels.value(row)] += weight;
        total += weight;
    }
    ClipAnswer answer;
    double best{-1.0};
    for (auto entry{byLabel.constBegin()}; entry != byLabel.constEnd(); ++entry) {
        if (entry.value() > best) {
            best = entry.value();
            answer.label = entry.key();
        }
    }
    answer.probability = total > 0.0 ? static_cast<float>(best / total) : 0.0F;
    return answer;
}

cv::Mat squareCrop(const cv::Mat &bgr, const cv::Rect &region)
{
    const int side{std::max(region.width, region.height)};
    if (bgr.empty() || side <= 0) {
        return {};
    }
    const cv::Rect square{region.x + (region.width - side) / 2,
                          region.y + (region.height - side) / 2,
                          side,
                          side};
    const cv::Rect inside{square & cv::Rect{0, 0, bgr.cols, bgr.rows}};
    if (inside.empty()) {
        return {};
    }
    cv::Mat crop;
    cv::copyMakeBorder(bgr(inside),
                       crop,
                       inside.y - square.y,
                       square.y + square.height - inside.y - inside.height,
                       inside.x - square.x,
                       square.x + square.width - inside.x - inside.width,
                       cv::BORDER_CONSTANT,
                       cv::Scalar{});
    return crop;
}

bool ClipClassifier::load(Accel::Device &device,
                          const QString &encoderPath,
                          const QString &labelsPath)
{
    m_model.reset();
    if (encoderPath.isEmpty()) {
        return false;
    }

    QFile file{labelsPath};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(clipLog) << "cannot read" << labelsPath;
        return false;
    }
    QString problem;
    std::optional<ClipLabels> labels{parseClipLabels(file.readAll(), problem)};
    if (!labels) {
        qCWarning(clipLog) << labelsPath << problem;
        return false;
    }
    if (labels->encoder != QFileInfo{encoderPath}.fileName()) {
        qCWarning(clipLog) << labelsPath << "was made for" << labels->encoder << "not"
                           << QFileInfo{encoderPath}.fileName();
        return false;
    }

    std::string error;
    std::unique_ptr<Accel::Model> model{device.openModel(encoderPath.toStdString(), error)};
    if (model == nullptr) {
        qCWarning(clipLog) << "CLIP encoder did not open:" << error.c_str();
        return false;
    }
    if (model->input().height != inputSide || model->input().width != inputSide ||
        model->outputs().size() != 1 ||
        static_cast<int>(model->outputs().front().elements()) != labels->dimension) {
        qCWarning(clipLog) << encoderPath << "does not match its labels";
        return false;
    }
    m_model = std::move(model);
    m_labels = std::move(*labels);
    m_reportedFailure = false;
    return true;
}

std::optional<ClipAnswer> ClipClassifier::classify(const cv::Mat &bgr,
                                                   const cv::Rect &region,
                                                   const QString &set)
{
    const auto found{m_labels.sets.constFind(set)};
    if (m_model == nullptr || found == m_labels.sets.cend()) {
        return std::nullopt;
    }
    const cv::Mat crop{squareCrop(bgr, region)};
    if (crop.empty()) {
        return std::nullopt;
    }

    cv::Mat resized;
    cv::resize(crop,
               resized,
               cv::Size{inputSide, inputSide},
               0.0,
               0.0,
               crop.cols > inputSide ? cv::INTER_AREA : cv::INTER_CUBIC);
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    const std::string error{runImage(*m_model, rgb, m_outputs)};
    if (!error.empty() || m_outputs.empty()) {
        if (!m_reportedFailure) {
            qCWarning(clipLog) << "CLIP inference failed:" << error.c_str();
            m_reportedFailure = true;
        }
        return std::nullopt;
    }
    std::vector<float> &values{m_outputs.front().values};
    const cv::Mat embedding{1, static_cast<int>(values.size()), CV_32F, values.data()};
    return zeroShot(embedding, found.value(), m_labels.logitScale);
}

} // namespace Kerfur
