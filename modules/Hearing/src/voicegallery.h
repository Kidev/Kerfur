// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VOICEGALLERY_H
#define KERFUR_VOICEGALLERY_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

namespace Kerfur {

class SpeakerEmbedder;

/// The enrolled voices, and the cosine match against them.
///
/// The counterpart of Vision's IdentityStore, over the same manifest: one
/// `identities/ids.json` entry names a person, their face and their voice, so a
/// name recognised by either half means the same person. Enrolment is cached
/// against a hash of each recording, because embedding a minute of audio takes
/// long enough to be worth not repeating on every start.
class VoiceGallery
{
public:
    struct Entry
    {
        QString name;
        QString sourceHash;
        cv::Mat embedding;
    };

    /// Embeds every recording in `directory` the cache does not already cover
    /// and rewrites the cache. Does nothing when the embedder is not loaded,
    /// rather than emptying a cache a working build had filled.
    void enrol(SpeakerEmbedder &embedder, const QString &directory, const QString &cachePath);

    /// The same, from the explicit `(name, recordingPath)` list the manifest
    /// produces. The name is the person's id, so a voice matched here and a
    /// face matched by IdentityStore report the same string for the same
    /// person, which is what the rank lookup depends on.
    void enrol(SpeakerEmbedder &embedder,
               const QList<QPair<QString, QString>> &entries,
               const QString &cachePath);

    /// The closest enrolled voice, or an empty name when nothing clears the
    /// threshold. `similarity` is written either way, so a near miss can be
    /// logged rather than merely denied.
    QString match(const cv::Mat &embedding, double &similarity) const;

    QStringList names() const;
    qsizetype size() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }

    void insert(const QString &name, const QString &sourceHash, const cv::Mat &embedding);
    void clear() { m_entries.clear(); }

    bool loadFrom(const QString &cachePath);
    bool saveTo(const QString &cachePath) const;

    void setThreshold(double threshold) { m_threshold = threshold; }
    double threshold() const { return m_threshold; }

private:
    QList<Entry> m_entries;

    // Measured against this model rather than taken from a paper. Two second
    // windows of a held out recording matched their own speaker at 0.65 and
    // above while the nearest of twenty eight other speakers reached 0.19, so
    // anything from roughly 0.3 to 0.6 separates them; the middle of that range
    // is the least sensitive place to stand.
    double m_threshold{0.45};
};

} // namespace Kerfur

#endif // KERFUR_VOICEGALLERY_H
