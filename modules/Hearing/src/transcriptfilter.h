// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_TRANSCRIPTFILTER_H
#define KERFUR_TRANSCRIPTFILTER_H

#include <QString>

namespace Kerfur {

/// Turns one raw Whisper segment into something worth showing, or into nothing.
///
/// Whisper describes what it hears rather than only transcribing it, so a
/// transcript legitimately contains `[BLANK_AUDIO]`, `(door closes)` and
/// `♪ music ♪`. Those are correct outputs and they are not speech, and a face
/// that announces "(silence)" is worse than one that says nothing.
///
/// The small models also loop: given a fragment they cannot resolve they will
/// emit the same word until the context runs out. A run of one word repeated
/// forty times is never something a person said.
///
/// Returns an empty string when nothing survived, which the caller must treat
/// as "heard nothing" and not as "heard an empty sentence".
QString cleanTranscript(const QString &raw);

/// Whether `text` contains the wake word in any of the shapes a small model
/// spells it. Case insensitive, and matched on word boundaries so that
/// "kerfur" is heard and "kerfuffle" is not.
bool containsWakeWord(const QString &text);

} // namespace Kerfur

#endif // KERFUR_TRANSCRIPTFILTER_H
