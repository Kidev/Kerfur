// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "transcriptfilter.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace Kerfur {

namespace {

// The same word this many times in a row is a decoder that has stopped
// listening. Three is deliberate: "no no no" is a thing people say, and
// "no no no no" is not what they said forty times.
constexpr int maximumRepeats{3};

const QRegularExpression &bracketedTag()
{
    // Whole bracketed spans, including the musical note pairs Whisper uses for
    // song lyrics. Anchored nowhere, because these appear mid sentence too.
    static const QRegularExpression pattern{
        QStringLiteral(R"(\[[^\]]*\]|\([^)]*\)|\*[^*]*\*|\x{266a}[^\x{266a}]*\x{266a})")};
    return pattern;
}

const QRegularExpression &whitespaceRun()
{
    static const QRegularExpression pattern{QStringLiteral(R"(\s+)")};
    return pattern;
}

const QRegularExpression &wakeWord()
{
    // The name is said the French way (a French u and r), the American way
    // (KUR-fer) and the British way (KUH-fuh, with no r). An English model
    // writes each as two syllables: a hard k, the vowel of "fur", an f, and a
    // swallowed vowel ("fur", "fer", "fa", "few", "fear"). English spells the
    // vowel of "fur" with an r even where nobody says it, so every first
    // syllable keeps one ("ker", "cur", "care", "kir"), which keeps "coffee"
    // and "Kafka" apart. The match ends on a word boundary, so
    // "kerfuffle" and "careful" do not wake the cat. "curve fur" and "kerf"
    // are what the tiny model hears in a fast "Kerfur".
    static const QRegularExpression pattern{
        QStringLiteral(
            R"(\b(?:)"
            R"((?:k|c|qu)(?:er|err|ehr|ur|urr|ir|irr|ear|air|aire|are|eur|oeur))"
            R"((?:[\s-]|,\s)?)"
            R"((?:f|ff|ph|v))"
            R"((?:ur|urr|\x{00fc}r|ure|er|err|eur|uhr|oor|our|ir|ear|eer|ier|a|ah|uh|ew|ue|u|or))"
            R"(|curve fur|kerf)\b)"),
        QRegularExpression::CaseInsensitiveOption};
    return pattern;
}

QString collapseRepeats(const QString &text)
{
    const QStringList words{text.split(QLatin1Char{' '}, Qt::SkipEmptyParts)};
    QStringList kept;
    kept.reserve(words.size());

    int run{0};
    for (const QString &word : words) {
        if (!kept.isEmpty()
            && kept.last().compare(word, Qt::CaseInsensitive) == 0) {
            ++run;
            if (run >= maximumRepeats) {
                continue;
            }
        } else {
            run = 0;
        }
        kept.append(word);
    }
    return kept.join(QLatin1Char{' '});
}

} // namespace

QString cleanTranscript(const QString &raw)
{
    QString text{raw};
    text.remove(bracketedTag());
    text.replace(whitespaceRun(), QStringLiteral(" "));
    text = text.trimmed();

    // Punctuation on its own is what is left of a tag that was already
    // removed, or of a segment that was never speech.
    const bool hasLetter{std::any_of(text.cbegin(), text.cend(), [](QChar character) {
        return character.isLetter();
    })};
    if (!hasLetter) {
        return {};
    }

    return collapseRepeats(text);
}

bool containsWakeWord(const QString &text)
{
    return wakeWord().match(text).hasMatch();
}

} // namespace Kerfur
