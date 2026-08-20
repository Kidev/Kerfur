// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "voicegallery.h"

#include "audiofile.h"
#include "speakerembedder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(voiceLog, "kerfur.hearing.voices")

const QStringList &recordingFilters()
{
    static const QStringList filters{QStringLiteral("*.mp3"),
                                     QStringLiteral("*.wav"),
                                     QStringLiteral("*.flac"),
                                     QStringLiteral("*.ogg"),
                                     QStringLiteral("*.m4a")};
    return filters;
}

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
    // cv::Mat::zeros rather than a braced constructor: OpenCV's Mat has an
    // initializer_list constructor, so three braced ints build a three element
    // vector of those integers instead of a 1 by n float matrix.
    cv::Mat embedding{cv::Mat::zeros(1, static_cast<int>(array.size()), CV_32F)};
    for (int column{0}; column < embedding.cols; ++column) {
        embedding.at<float>(0, column) = static_cast<float>(array.at(column).toDouble());
    }
    return embedding;
}

} // namespace

void VoiceGallery::insert(const QString &name, const QString &sourceHash, const cv::Mat &embedding)
{
    if (name.isEmpty() || embedding.empty()) {
        return;
    }
    m_entries.append(Entry{name, sourceHash, embedding.clone()});
}

QStringList VoiceGallery::names() const
{
    QStringList list;
    list.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        list.append(entry.name);
    }
    return list;
}

QString VoiceGallery::match(const cv::Mat &embedding, double &similarity) const
{
    similarity = -1.0;
    if (embedding.empty() || m_entries.isEmpty()) {
        return {};
    }

    QString best;
    for (const Entry &entry : m_entries) {
        const double score{SpeakerEmbedder::cosineSimilarity(embedding, entry.embedding)};
        if (score > similarity) {
            similarity = score;
            best = entry.name;
        }
    }
    return similarity >= m_threshold ? best : QString{};
}

void VoiceGallery::enrol(SpeakerEmbedder &embedder,
                         const QString &directory,
                         const QString &cachePath)
{
    // Before the cache is touched. Without a model every recording would fail
    // to embed, the refreshed list would come out empty, and a build that
    // happens to lack the model would silently destroy the enrolment of one
    // that has it.
    if (!embedder.isLoaded()) {
        qCInfo(voiceLog) << "no speaker model; voices not enrolled";
        return;
    }

    loadFrom(cachePath);

    const QDir gallery{directory};
    if (directory.isEmpty() || !gallery.exists()) {
        qCInfo(voiceLog) << "no voice gallery at" << directory;
        return;
    }

    const QFileInfoList recordings{
        gallery.entryInfoList(recordingFilters(), QDir::Files | QDir::Readable, QDir::Name)};

    QList<Entry> refreshed;
    refreshed.reserve(recordings.size());
    bool changed{false};

    for (const QFileInfo &recording : recordings) {
        const QString name{recording.completeBaseName()};
        const QString hash{hashFile(recording.absoluteFilePath())};
        if (hash.isEmpty()) {
            continue;
        }

        // Keyed on content rather than on modification time, so a gallery
        // restored from a backup or checked out fresh does not re-embed
        // everything because its timestamps moved.
        const auto cached{std::find_if(m_entries.cbegin(),
                                       m_entries.cend(),
                                       [&name, &hash](const Entry &entry) {
                                           return entry.name == name && entry.sourceHash == hash;
                                       })};
        if (cached != m_entries.cend() && !cached->embedding.empty()) {
            refreshed.append(*cached);
            continue;
        }

        QString failure;
        const std::vector<float> samples{
            decodeMonoAudio(recording.absoluteFilePath(), SpeakerEmbedder::sampleRate, &failure)};
        if (samples.empty()) {
            qCWarning(voiceLog) << "cannot decode" << recording.fileName() << failure;
            continue;
        }

        const cv::Mat embedding{embedder.embed(samples)};
        if (embedding.empty()) {
            qCWarning(voiceLog) << "cannot embed" << recording.fileName()
                                << "; needs at least" << SpeakerEmbedder::minimumSeconds
                                << "seconds of speech";
            continue;
        }

        refreshed.append(Entry{name, hash, embedding});
        changed = true;
        qCInfo(voiceLog) << "enrolled voice" << name << "from"
                         << samples.size() / SpeakerEmbedder::sampleRate << "seconds";
    }

    changed = changed || refreshed.size() != m_entries.size();
    m_entries = refreshed;
    if (changed) {
        saveTo(cachePath);
    }
    qCInfo(voiceLog) << "voice gallery holds" << m_entries.size() << "voices";
}

bool VoiceGallery::loadFrom(const QString &cachePath)
{
    QFile file{cachePath};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document{QJsonDocument::fromJson(file.readAll())};
    if (!document.isObject()) {
        return false;
    }

    m_entries.clear();
    const QJsonArray voices{document.object().value(QStringLiteral("voices")).toArray()};
    for (const QJsonValue &value : voices) {
        const QJsonObject entry{value.toObject()};
        const QString name{entry.value(QStringLiteral("name")).toString()};
        const cv::Mat embedding{
            embeddingFromJson(entry.value(QStringLiteral("embedding")).toArray())};
        if (name.isEmpty() || embedding.empty()) {
            continue;
        }
        m_entries.append(
            Entry{name, entry.value(QStringLiteral("sha256")).toString(), embedding});
    }
    return true;
}

bool VoiceGallery::saveTo(const QString &cachePath) const
{
    if (cachePath.isEmpty()) {
        return false;
    }
    QDir{}.mkpath(QFileInfo{cachePath}.absolutePath());

    QJsonArray voices;
    for (const Entry &entry : m_entries) {
        voices.append(QJsonObject{{QStringLiteral("name"), entry.name},
                                  {QStringLiteral("sha256"), entry.sourceHash},
                                  {QStringLiteral("embedding"), embeddingToJson(entry.embedding)}});
    }

    // QSaveFile, so that an interrupted write leaves the previous cache rather
    // than a truncated one that would be read back as an empty gallery.
    QSaveFile file{cachePath};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument{QJsonObject{{QStringLiteral("voices"), voices}}}.toJson(
        QJsonDocument::Compact));
    return file.commit();
}

} // namespace Kerfur
