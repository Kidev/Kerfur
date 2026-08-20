// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitystore.h"

#include "facepipeline.h"

#include "identityregistry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(identityLog, "kerfur.vision.identity")

QString hashFile(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QJsonArray embeddingToJson(const cv::Mat &embedding)
{
    QJsonArray array;
    const cv::Mat flat{embedding.reshape(1, 1)};
    for (int column{0}; column < flat.cols; ++column) {
        array.append(static_cast<double>(flat.at<float>(0, column)));
    }
    return array;
}

cv::Mat embeddingFromJson(const QJsonArray &array)
{
    if (array.isEmpty()) {
        return {};
    }
    // cv::Mat::zeros rather than `cv::Mat embedding{1, n, CV_32F}`. OpenCV's
    // Mat has an initializer_list constructor, so a braced list of three ints
    // builds a three element column vector of those integers instead of a
    // 1 by n float matrix, and every value read out of it afterwards is a
    // reinterpreted integer that still looks like a number.
    cv::Mat embedding{cv::Mat::zeros(1, static_cast<int>(array.size()), CV_32F)};
    for (int column{0}; column < embedding.cols; ++column) {
        embedding.at<float>(0, column) = static_cast<float>(array.at(column).toDouble());
    }
    return embedding;
}

} // namespace

double IdentityStore::cosineSimilarity(const cv::Mat &left, const cv::Mat &right)
{
    if (left.empty() || right.empty() || left.total() != right.total()) {
        return 0.0;
    }

    const cv::Mat a{left.reshape(1, 1)};
    const cv::Mat b{right.reshape(1, 1)};
    const double dot{a.dot(b)};
    const double magnitude{cv::norm(a) * cv::norm(b)};
    if (magnitude <= 0.0) {
        return 0.0;
    }
    return dot / magnitude;
}

void IdentityStore::enrol(FacePipeline &pipeline,
                          const QString &galleryDirectory,
                          const QString &cachePath)
{
    const IdentityRegistry registry{IdentityRegistry::load(galleryDirectory)};
    if (!registry.problem().isEmpty()) {
        qCInfo(identityLog) << registry.problem();
    }
    enrol(pipeline, registry.faceEntries(), cachePath);
}

void IdentityStore::enrol(FacePipeline &pipeline,
                          const QList<QPair<QString, QString>> &entries,
                          const QString &cachePath)
{
    // Before the cache is touched. Without a recognition model every image
    // would fail to embed, the refreshed list would come out empty, and the
    // cache would be rewritten with nothing in it, so a build that happens to
    // lack the model would silently destroy the enrolment of one that has it.
    if (!pipeline.hasRecognizer()) {
        qCInfo(identityLog) << "no recognition model; gallery not enrolled";
        return;
    }

    loadFrom(cachePath);

    if (entries.isEmpty()) {
        qCInfo(identityLog) << "no faces declared; gallery not enrolled";
        return;
    }

    QList<Entry> refreshed;
    refreshed.reserve(entries.size());
    bool changed{false};

    for (const auto &[name, path] : entries) {
        const QString hash{hashFile(path)};
        if (hash.isEmpty()) {
            qCWarning(identityLog) << "cannot read" << path;
            continue;
        }

        // Keyed on content rather than on modification time, so a gallery
        // restored from a backup or checked out fresh does not re-embed
        // everything because its timestamps moved.
        const auto cached{std::find_if(m_entries.cbegin(),
                                       m_entries.cend(),
                                       [&name = name, &hash](const Entry &entry) {
                                           return entry.name == name
                                                  && entry.sourceHash == hash;
                                       })};
        if (cached != m_entries.cend() && !cached->embedding.empty()) {
            refreshed.append(*cached);
            continue;
        }

        const cv::Mat picture{cv::imread(path.toStdString(), cv::IMREAD_COLOR)};
        if (picture.empty()) {
            qCWarning(identityLog) << "cannot decode" << path;
            continue;
        }

        const std::optional<cv::Mat> embedding{pipeline.embedLargestFace(picture)};
        if (!embedding.has_value()) {
            qCWarning(identityLog) << "no face found in" << path;
            continue;
        }

        refreshed.append(Entry{name, hash, *embedding});
        changed = true;
    }

    if (changed || refreshed.size() != m_entries.size()) {
        m_entries = refreshed;
        saveTo(cachePath);
    } else {
        m_entries = refreshed;
    }
    qCInfo(identityLog) << "gallery holds" << m_entries.size() << "identities";
}

QString IdentityStore::match(const cv::Mat &embedding, double &similarity) const
{
    similarity = 0.0;
    if (embedding.empty() || m_entries.isEmpty()) {
        return {};
    }

    QString best;
    for (const Entry &entry : m_entries) {
        const double score{cosineSimilarity(embedding, entry.embedding)};
        if (score > similarity) {
            similarity = score;
            best = entry.name;
        }
    }
    return similarity >= m_threshold ? best : QString{};
}

bool IdentityStore::loadFrom(const QString &cachePath)
{
    m_entries.clear();
    QFile file{cachePath};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document{QJsonDocument::fromJson(file.readAll())};
    // Copy initialization rather than braces, deliberately. QJsonValue converts
    // implicitly from QJsonArray, so `QJsonArray a{someArray}` picks the
    // initializer list constructor and yields an array of one element that is
    // the original array, which then reads back as an empty gallery.
    const QJsonArray entries = document.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object{value.toObject()};
        Entry entry;
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.sourceHash = object.value(QStringLiteral("sha256")).toString();
        entry.embedding = embeddingFromJson(object.value(QStringLiteral("embedding")).toArray());
        if (!entry.name.isEmpty() && !entry.embedding.empty()) {
            m_entries.append(entry);
        }
    }
    return !m_entries.isEmpty();
}

bool IdentityStore::saveTo(const QString &cachePath) const
{
    const QFileInfo info{cachePath};
    if (!QDir{}.mkpath(info.absolutePath())) {
        qCWarning(identityLog) << "cannot create" << info.absolutePath();
        return false;
    }

    QJsonArray entries;
    for (const Entry &entry : m_entries) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), entry.name);
        object.insert(QStringLiteral("sha256"), entry.sourceHash);
        object.insert(QStringLiteral("embedding"), embeddingToJson(entry.embedding));
        entries.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("entries"), entries);

    // QSaveFile so that a crash midway through leaves the previous cache
    // intact. A truncated cache would be read back as an empty gallery, which
    // silently turns every known person into a stranger.
    QSaveFile file{cachePath};
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(identityLog) << "cannot write" << cachePath;
        return false;
    }
    file.write(QJsonDocument{root}.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qCWarning(identityLog) << "cannot commit" << cachePath;
        return false;
    }
    return true;
}

void IdentityStore::insert(const QString &name, const QString &sourceHash,
                           const cv::Mat &embedding)
{
    if (name.isEmpty() || embedding.empty()) {
        return;
    }
    m_entries.append(Entry{name, sourceHash, embedding.clone()});
}

void IdentityStore::clear()
{
    m_entries.clear();
}

} // namespace Kerfur
