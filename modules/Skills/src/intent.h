// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_INTENT_H
#define KERFUR_INTENT_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace Kerfur {

/// One thing a skill can be asked to do, as the matcher sees it.
///
/// The `description` is the sentence the skill author wrote -- "when asked
/// about adding calories" -- and it is not decoration. It is the only part of a
/// trigger a language model would be shown, and it is what the built in matcher
/// falls back to when the author wrote no phrases at all. Writing it as a
/// description of the request rather than as a name is therefore the whole
/// interface.
struct IntentSpec
{
    /// Unique within the registry. The trigger's own `intent` string, so that
    /// a resolution can be routed back without a second lookup table.
    QString id;

    /// What asking for this sounds like, in English.
    QString description;

    /// Concrete patterns, with `{slot}` placeholders. Optional: a trigger with
    /// none is matched on the description alone, which is looser and is exactly
    /// what a skill author wants for a question that has no arguments.
    QStringList phrases;

    /// The skill this belongs to, for logging and for the panel.
    QString skill;
};

/// What the matcher concluded about one utterance.
struct IntentResolution
{
    /// Empty when nothing was recognised. That is a normal outcome and not an
    /// error: most of what a room says to a robot is not addressed to it.
    QString id;
    QVariantMap captures;
    double confidence{0.0};

    /// Free text the backend wants said instead of running anything. Always
    /// empty from the pattern matcher; a language model backend is where this
    /// becomes the answer to a question no trigger covers.
    QString reply;
};

/// Lowercased, stripped of punctuation, with runs of whitespace collapsed.
/// Every comparison in this file happens on this form.
QString normalizeUtterance(const QString &text);

/// A quantity spoken either as digits or as English words.
///
/// Whisper writes "two hundred" as often as it writes "200", and a skill that
/// only understood one of the two would work in testing and fail in the
/// kitchen. Returns an invalid QVariant when the text holds no number.
QVariant parseQuantity(const QString &text);

/// Whether a short reply means yes, or means no. Both can be false, which is
/// the answer for "hang on" and is the reason there are two functions rather
/// than one boolean.
bool isAffirmative(const QString &text);
bool isNegative(const QString &text);

/// Matches one utterance against every intent and returns the best.
///
/// Ranked by specificity rather than by score alone: a phrase with six literal
/// words beats one with two, because the six word phrase can only have matched
/// if the speaker said all six. A score would have to encode that anyway, and
/// encoding it as a number invites tuning something that is not a preference.
IntentResolution matchIntent(const QString &utterance, const QList<IntentSpec> &intents);

/// The slot names a phrase declares, in order of appearance.
QStringList slotsOfPhrase(const QString &phrase);

/// The words of a description that carry meaning -- everything that is not a
/// stop word. Exposed for the tests, which is the only way to check that
/// "when asked about the weather" reduces to "weather" and not to "asked".
QStringList contentWords(const QString &text);

} // namespace Kerfur

#endif // KERFUR_INTENT_H
