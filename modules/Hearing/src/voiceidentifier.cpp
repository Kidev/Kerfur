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

    const QString gallery{IdentityDirectory::resolve()};
    if (gallery.isEmpty()) {
        emit ready(true,
                   {},
                   QStringLiteral("No identities gallery; searched %1")
                       .arg(IdentityDirectory::describeSearchPath()));
        return;
    }

    m_gallery.enrol(m_embedder, gallery, cachePath());
    qCInfo(voiceIdLog) << "voices from" << gallery << m_gallery.names();
    emit ready(true, m_gallery.names(), QString{});
}

void VoiceIdentifier::identify(const std::vector<float> &samples, quint64 sequence)
{
    if (!m_embedder.isLoaded() || m_gallery.isEmpty()) {
        emit identified(QString{}, -1.0, sequence);
        return;
    }

    const cv::Mat embedding{m_embedder.embed(samples)};
    if (embedding.empty()) {
        // Too short to say anything about. Reported as an unknown speaker with
        // no similarity rather than as a mismatch, because the two are
        // different facts and only one of them is about the person.
        emit identified(QString{}, -1.0, sequence);
        return;
    }

    double similarity{-1.0};
    const QString name{m_gallery.match(embedding, similarity)};
    qCDebug(voiceIdLog) << "utterance" << sequence << "matched"
                        << (name.isEmpty() ? QStringLiteral("nobody") : name) << "at" << similarity;
    emit identified(name, similarity, sequence);
}

} // namespace Kerfur
