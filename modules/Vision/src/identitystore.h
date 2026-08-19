// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_IDENTITYSTORE_H
#define KERFUR_IDENTITYSTORE_H

#include <QList>
#include <QString>

#include <opencv2/core.hpp>

namespace Kerfur {

class FacePipeline;

/// The gallery of known faces and the cosine match against it.
///
/// Enrolment is cached against a hash of each source image, so adding a face to
/// the gallery re-embeds one file rather than all of them, and an unchanged
/// gallery costs a JSON read at startup instead of one inference per person.
class IdentityStore
{
public:
    struct Entry
    {
        QString name;
        QString sourceHash;
        cv::Mat embedding;
    };

    /// Embeds every image in `galleryDirectory` that the cache does not already
    /// cover, and rewrites the cache. Silently does nothing useful when the
    /// pipeline has no recognition model, which is the wasm case.
    void enrol(FacePipeline &pipeline, const QString &galleryDirectory, const QString &cachePath);

    /// The closest gallery entry, or an empty name when nothing clears the
    /// threshold. `similarity` is written in both cases so a caller can log how
    /// near a miss was.
    QString match(const cv::Mat &embedding, double &similarity) const;

    qsizetype size() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }

    void setThreshold(double threshold) { m_threshold = threshold; }
    double threshold() const { return m_threshold; }

    /// Adds one identity directly. The embedding is copied, because the
    /// recogniser hands back a view onto a buffer it reuses.
    void insert(const QString &name, const QString &sourceHash, const cv::Mat &embedding);

    void clear();

    /// The cache is the on disk form of the gallery. Public because enrolment
    /// is not the only way a store gets filled, and because a round trip
    /// through it is the part worth testing without a model file.
    bool loadFrom(const QString &cachePath);
    bool saveTo(const QString &cachePath) const;

    static double cosineSimilarity(const cv::Mat &left, const cv::Mat &right);

private:

    QList<Entry> m_entries;

    // OpenCV's documented cosine threshold for SFace. Below it two embeddings
    // are held to be different people, and it is deliberately not a tuning knob
    // exposed to the control panel: moving it changes what "known" means for
    // every consumer at once, including Home Assistant.
    double m_threshold{0.363};
};

} // namespace Kerfur

#endif // KERFUR_IDENTITYSTORE_H
