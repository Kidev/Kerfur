// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_MODELCATALOG_H
#define KERFUR_MODELCATALOG_H

#include <QHash>
#include <QString>
#include <QStringList>

namespace Kerfur {

/// The inference stages of the CPU backend, named after the stages in section
/// 5.3 of the perception design so that the two documents can be read against
/// each other.
enum class ModelId {
    Pose,          ///< stage A, person boxes and COCO keypoints
    Objects,       ///< stage B, COCO 80 classes
    Face,          ///< stage C, face box and five landmarks
    Identity,      ///< stage D, face embedding
    Emotion,       ///< stage E, facial expression
    Palm,          ///< stage F, palm boxes inside a proposed region
    HandLandmarks  ///< stage F, twenty-one hand points from a palm
};

/// Finds the model files on disk and reports which stages can therefore run.
///
/// A missing file disables one stage and is logged; it never fails a start.
/// The models are provisioned separately from the binary by `make models`, so
/// running the face with an incomplete model directory is a normal state during
/// development, not an error condition.
class ModelCatalog
{
public:
    /// Looks up the directory through Core's ModelDirectory, which is where the
    /// search order lives, and reports which stage files it holds. Only the one
    /// directory is searched, so a half populated override is visible as
    /// missing stages rather than silently completed from somewhere else.
    static ModelCatalog resolve();

    /// The file name each stage expects. Duplicated in `assets/models.json`,
    /// which is the provisioning manifest; tst_modelcatalog asserts they agree.
    static QString fileName(ModelId id);

    static QString stageName(ModelId id);

    QString directory() const { return m_directory; }
    QString path(ModelId id) const { return m_paths.value(id); }
    bool has(ModelId id) const { return !m_paths.value(id).isEmpty(); }

    /// Every stage whose file was not found, for one log line at startup rather
    /// than one per stage scattered through initialization.
    QStringList missingStages() const;

private:
    QString m_directory;
    QHash<ModelId, QString> m_paths;
};

} // namespace Kerfur

#endif // KERFUR_MODELCATALOG_H
