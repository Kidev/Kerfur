// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_AUDIOFILE_H
#define KERFUR_AUDIOFILE_H

#include <QString>

#include <vector>

namespace Kerfur {

/// Decodes an audio file to mono 16 kHz floats in [-1, 1].
///
/// Anything Qt Multimedia can open, which on a desktop build is the FFmpeg
/// backend and therefore mp3, wav, flac and the rest. Used to enrol the voice
/// half of the identity gallery, where the files are whatever the person
/// happened to record in.
///
/// Blocking: it runs a nested event loop until the decoder finishes, because
/// enrolment happens once at start up and every caller wants the samples rather
/// than a promise of them. Returns empty on any failure, with the reason in
/// `error` when one is given.
std::vector<float> decodeMonoAudio(const QString &path,
                                   int sampleRate = 16000,
                                   QString *error = nullptr);

} // namespace Kerfur

#endif // KERFUR_AUDIOFILE_H
