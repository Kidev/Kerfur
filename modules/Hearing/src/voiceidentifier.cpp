// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "voiceidentifier.h"

#include "identitydirectory.h"
#include "modeldirectory.h"

#include <QLoggingCategory>
#include <QStandardPaths>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(voiceIdLog, "kerfur.hearing.voiceid")

// The speech entry of assets/models.json for the speaker network.
constexpr QLatin1StringView speakerModelFile{"voxceleb_resnet34_LM.onnx"};

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/voices.json");
}

} // namespace

VoiceIdentifier::VoiceIdentifier(QObject *parent) : QObject{parent}
{
    qRegisterMetaType<std::vector<float>>("std::vector<float>");
}

void VoiceIdentifier::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    const QString modelPath{ModelDirectory::locate(speakerModelFile)};
    if (modelPath.isEmpty()) {
        emit ready(false,
                   {},
                   QStringLiteral("Speaker model %1 not found. Searched %2. "
                                  "Run `make models` to provision it.")
                       .arg(speakerModelFile, ModelDirectory::describeSearchPath()));
        return;
    }

    if (!m_embedder.load(modelPath)) {
        emit ready(false,
                   {},
                   QStringLiteral("Speaker model %1 could not be loaded").arg(modelPath));
        return;
    }

    enrolGallery();
}

void VoiceIdentifier::reloadGallery()
{
    // Without a model nothing can be enrolled, and `ready` has already said
    // why.
    if (!m_initialized || !m_embedder.isLoaded()) {
        return;
    }
    enrolGallery();
}

void VoiceIdentifier::enrolGallery()
{
    const QStringList galleries{IdentityDirectory::galleries()};
    if (galleries.isEmpty()) {
        m_registry = IdentityRegistry{};
        m_gallery.clear();
        emit ready(true,
                   {},
                   QStringLiteral("No identities gallery; searched %1")
                       .arg(IdentityDirectory::describeSearchPath()));
        return;
    }

    m_registry = IdentityRegistry::load(galleries);
    if (!m_registry.problem().isEmpty()) {
        qCInfo(voiceIdLog) << m_registry.problem();
    }
    m_gallery.enrol(m_embedder, m_registry.voiceEntries(), cachePath());
    qCInfo(voiceIdLog) << "voices from" << galleries << m_gallery.names();
    emit ready(true, m_gallery.names(), QString{});
}

void VoiceIdentifier::identify(const std::vector<float> &samples, quint64 sequence)
{
    if (!m_embedder.isLoaded() || m_gallery.isEmpty()) {
        emit identified(QString{}, -1.0, IdentityRegistry::unknownPriority, QString{}, sequence);
        return;
    }

    const cv::Mat embedding{m_embedder.embed(samples)};
    if (embedding.empty()) {
        // Too short to say anything about. Reported as an unknown speaker with
        // no similarity rather than as a mismatch, because the two are
        // different facts and only one of them is about the person.
        emit identified(QString{}, -1.0, IdentityRegistry::unknownPriority, QString{}, sequence);
        return;
    }

    double similarity{-1.0};
    const QString name{m_gallery.match(embedding, similarity)};
    const int priority{m_registry.priorityOf(name)};
    qCDebug(voiceIdLog) << "utterance" << sequence << "matched"
                        << (name.isEmpty() ? QStringLiteral("nobody") : name) << "at" << similarity
                        << "rank" << priority;
    emit identified(name,
                    similarity,
                    priority,
                    name.isEmpty() ? QString{} : m_registry.standingOf(name),
                    sequence);
}

} // namespace Kerfur
