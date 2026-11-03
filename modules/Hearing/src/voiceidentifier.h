// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VOICEIDENTIFIER_H
#define KERFUR_VOICEIDENTIFIER_H

#include "identityregistry.h"
#include "speakerembedder.h"
#include "voicegallery.h"

#include <QObject>
#include <QStringList>

#include <vector>

namespace Kerfur {

/// Puts a name to a voice, on a thread that is not the one drawing the face.
///
/// It is given the same utterances the recogniser transcribes, because the
/// segmentation problem is identical and solving it twice would mean two
/// answers about when somebody stopped talking. A ResNet over several seconds
/// of audio is tens of milliseconds; the gallery enrolment behind it is
/// seconds, which is the reason this has a thread rather than a function.
class VoiceIdentifier : public QObject
{
    Q_OBJECT

public:
    explicit VoiceIdentifier(QObject *parent = nullptr);

public slots:
    /// Loads the model and enrols the gallery. Emits `ready` in every case,
    /// including the one where nothing loaded.
    void initialize();

    /// Reads the galleries again and re-enrols, then emits `ready` with the
    /// new list. Connected to `IdentityLibrary::galleryChanged`; a recording
    /// whose file is unchanged is answered from the cache.
    void reloadGallery();

    /// One complete utterance of 16 kHz mono samples. Utterances shorter than
    /// the embedder's minimum are answered with an empty name rather than
    /// dropped, so that a caller waiting on a result is never left waiting.
    void identify(const std::vector<float> &samples, quint64 sequence);

signals:
    void ready(bool loaded, const QStringList &voices, const QString &problem);
    /// `priority` is the rank `identities/ids.json` gives this voice, zero
    /// being the top and `IdentityRegistry::unknownPriority` a stranger, and
    /// `title` is what the same manifest calls them -- "owner", "trusted" --
    /// or the rank spelled out when it calls them nothing. Both come from here
    /// rather than being looked up by the receiver because the manifest is
    /// already open on this side, and because a standing that travels with the
    /// name can never disagree with it.
    void identified(const QString &name,
                    double similarity,
                    int priority,
                    const QString &title,
                    quint64 sequence);

private:
    void enrolGallery();

    SpeakerEmbedder m_embedder;
    VoiceGallery m_gallery;
    IdentityRegistry m_registry;
    bool m_initialized{false};
};

} // namespace Kerfur

// No Q_DECLARE_METATYPE here. Qt already declares every std::vector as a
// sequential container metatype in qmetatype.h, so a second declaration is a
// redefinition of the same specialization rather than a missing one being
// supplied. What a queued connection still needs is the name lookup, and
// VoiceIdentifier's constructor registers that.

#endif // KERFUR_VOICEIDENTIFIER_H
