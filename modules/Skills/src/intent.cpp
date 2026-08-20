// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "intent.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace Kerfur {

namespace {

/// Words that say nothing about which intent was meant.
///
/// Deliberately short. A stop list that removes "set", "add" or "current"
/// would remove the three words that distinguish the three calorie triggers
/// from each other, and a matcher that cannot tell those apart is worse than
/// no matcher.
const QSet<QString> &stopWords()
{
    static const QSet<QString> words{
        QStringLiteral("a"),     QStringLiteral("an"),     QStringLiteral("the"),
        QStringLiteral("is"),    QStringLiteral("are"),    QStringLiteral("was"),
        QStringLiteral("be"),    QStringLiteral("been"),   QStringLiteral("am"),
        QStringLiteral("i"),     QStringLiteral("me"),     QStringLiteral("my"),
        QStringLiteral("you"),   QStringLiteral("your"),   QStringLiteral("it"),
        QStringLiteral("of"),    QStringLiteral("to"),     QStringLiteral("for"),
        QStringLiteral("in"),    QStringLiteral("on"),     QStringLiteral("at"),
        QStringLiteral("and"),   QStringLiteral("or"),     QStringLiteral("that"),
        QStringLiteral("this"),  QStringLiteral("when"),   QStringLiteral("asked"),
        QStringLiteral("ask"),   QStringLiteral("about"),  QStringLiteral("please"),
        QStringLiteral("hey"),   QStringLiteral("ok"),     QStringLiteral("okay"),
        QStringLiteral("kerfur"), QStringLiteral("do"),    QStringLiteral("does"),
        QStringLiteral("can"),   QStringLiteral("could"),  QStringLiteral("would"),
        QStringLiteral("will"),  QStringLiteral("us"),     QStringLiteral("we"),
    };
    return words;
}

const QHash<QString, qint64> &numberWords()
{
    static const QHash<QString, qint64> words{
        {QStringLiteral("zero"), 0},        {QStringLiteral("one"), 1},
        {QStringLiteral("two"), 2},         {QStringLiteral("three"), 3},
        {QStringLiteral("four"), 4},        {QStringLiteral("five"), 5},
        {QStringLiteral("six"), 6},         {QStringLiteral("seven"), 7},
        {QStringLiteral("eight"), 8},       {QStringLiteral("nine"), 9},
        {QStringLiteral("ten"), 10},        {QStringLiteral("eleven"), 11},
        {QStringLiteral("twelve"), 12},     {QStringLiteral("thirteen"), 13},
        {QStringLiteral("fourteen"), 14},   {QStringLiteral("fifteen"), 15},
        {QStringLiteral("sixteen"), 16},    {QStringLiteral("seventeen"), 17},
        {QStringLiteral("eighteen"), 18},   {QStringLiteral("nineteen"), 19},
        {QStringLiteral("twenty"), 20},     {QStringLiteral("thirty"), 30},
        {QStringLiteral("forty"), 40},      {QStringLiteral("fifty"), 50},
        {QStringLiteral("sixty"), 60},      {QStringLiteral("seventy"), 70},
        {QStringLiteral("eighty"), 80},     {QStringLiteral("ninety"), 90},
    };
    return words;
}

// The confidence a description-only match has to reach. Two thirds of the
// content words, so "when asked about the current calories" fires on "what are
// my current calories" and not on "the calories were nice".
constexpr double descriptionFloor{0.66};

QStringList tokenize(const QString &text)
{
    return normalizeUtterance(text).split(QLatin1Char{' '}, Qt::SkipEmptyParts);
}

/// The regular expression one phrase becomes.
///
/// Literal words are required, in order, and anything may sit between them:
/// people say "hey kerfur could you add three hundred calories for me please"
/// and mean "add {amount} calories". A slot swallows as little as it can, which
/// is what stops `{amount}` in "add {amount} calories" from eating the word
/// "calories" as well.
QRegularExpression phraseToRegex(const QString &phrase, QStringList &names)
{
    names = slotsOfPhrase(phrase);

    const QStringList tokens{phrase.toLower().split(QLatin1Char{' '}, Qt::SkipEmptyParts)};
    const auto isSlot = [](const QString &token) {
        return token.startsWith(QLatin1Char{'{'}) && token.endsWith(QLatin1Char{'}'});
    };

    QString pattern;
    for (qsizetype index{0}; index < tokens.size(); ++index) {
        const QString &token{tokens.at(index)};

        if (index > 0) {
            // Filler between two literal words, and nothing at all next to a
            // slot. A slot is already the gap; putting a second one beside it
            // lets the filler swallow the value and hand back a single
            // character, which is exactly what "add three hundred calories"
            // used to do.
            const bool besideSlot{isSlot(token) || isSlot(tokens.at(index - 1))};
            pattern += besideSlot ? QStringLiteral("\\s+")
                                  : QStringLiteral("(?:\\s+\\S+){0,3}\\s+");
        }

        if (isSlot(token)) {
            const QString name{token.mid(1, token.size() - 2)};
            // Named groups, so the caller reads slots by the name the author
            // wrote rather than by counting brackets.
            //
            // Lazy in the middle, so that the value stops at the literal that
            // follows it. Greedy at the end, where there is no following
            // literal to stop at and a lazy group would capture one digit of
            // "2000".
            pattern += isSlot(tokens.last()) && index == tokens.size() - 1
                           ? QStringLiteral("(?<%1>.+)").arg(name)
                           : QStringLiteral("(?<%1>.+?)").arg(name);
        } else {
            const QString word{normalizeUtterance(token)};
            if (word.isEmpty()) {
                continue;
            }
            pattern += QStringLiteral("\\b") + QRegularExpression::escape(word)
                       + QStringLiteral("\\b");
        }
    }

    return QRegularExpression{pattern, QRegularExpression::CaseInsensitiveOption};
}

int literalWordsOf(const QString &phrase)
{
    int count{0};
    for (const QString &token : phrase.split(QLatin1Char{' '}, Qt::SkipEmptyParts)) {
        if (!token.startsWith(QLatin1Char{'{'})) {
            ++count;
        }
    }
    return count;
}

/// Turns a captured slot into the most specific value it can be. A number if it
/// reads as one, the trimmed text otherwise, because a skill asking for a
/// person's name and one asking for a quantity both write `{value}`.
QVariant refineSlot(const QString &captured)
{
    const QVariant quantity{parseQuantity(captured)};
    if (quantity.isValid()) {
        return quantity;
    }
    return captured.trimmed();
}

} // namespace

QString normalizeUtterance(const QString &text)
{
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            cleaned.append(character.toLower());
        } else if (character == QLatin1Char{'.'} || character == QLatin1Char{','}) {
            // Kept only between digits, where it is part of the number rather
            // than punctuation: "3,000" and "3.5" survive, "calories." does not
            // keep its stop.
            const bool betweenDigits{!cleaned.isEmpty() && cleaned.back().isDigit()};
            cleaned.append(betweenDigits ? character : QLatin1Char{' '});
        } else if (character == QLatin1Char{'-'} || character == QLatin1Char{'\''}) {
            cleaned.append(QLatin1Char{' '});
        } else {
            cleaned.append(QLatin1Char{' '});
        }
    }

    // A separator left dangling by the rule above -- "3," at the end of a
    // sentence -- is punctuation after all.
    while (!cleaned.isEmpty()
           && (cleaned.back() == QLatin1Char{'.'} || cleaned.back() == QLatin1Char{','})) {
        cleaned.chop(1);
    }

    return cleaned.simplified();
}

QStringList contentWords(const QString &text)
{
    QStringList words;
    for (const QString &token : tokenize(text)) {
        if (token.startsWith(QLatin1Char{'{'}) || stopWords().contains(token)) {
            continue;
        }
        if (!words.contains(token)) {
            words.append(token);
        }
    }
    return words;
}

QStringList slotsOfPhrase(const QString &phrase)
{
    QStringList names;
    static const QRegularExpression placeholder{QStringLiteral("\\{([A-Za-z_][A-Za-z0-9_]*)\\}")};
    QRegularExpressionMatchIterator matches{placeholder.globalMatch(phrase)};
    while (matches.hasNext()) {
        const QString name{matches.next().captured(1)};
        if (!names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

QVariant parseQuantity(const QString &text)
{
    const QString cleaned{normalizeUtterance(text)};
    if (cleaned.isEmpty()) {
        return {};
    }

    // Digits win outright when they are there. A sentence holding both a
    // numeral and a number word -- "add 300, no wait, four hundred" -- is
    // ambiguous no matter what, and taking the numeral is at least a rule
    // somebody can predict.
    static const QRegularExpression digits{QStringLiteral("(-?\\d+(?:[.,]\\d+)*)")};
    const QRegularExpressionMatch digitMatch{digits.match(cleaned)};
    if (digitMatch.hasMatch()) {
        QString number{digitMatch.captured(1)};
        // A comma is a thousands separator and a full stop is a decimal point.
        // English only, which is what the recogniser is configured for.
        number.remove(QLatin1Char{','});
        bool ok{false};
        const double parsed{number.toDouble(&ok)};
        if (ok) {
            return parsed;
        }
    }

    // Words. Accumulates in the English way: units and tens add into the
    // current group, "hundred" multiplies the group, "thousand" and "million"
    // close it into the running total.
    qint64 total{0};
    qint64 group{0};
    bool sawNumber{false};
    for (const QString &token : cleaned.split(QLatin1Char{' '}, Qt::SkipEmptyParts)) {
        if (token == QStringLiteral("and") && sawNumber) {
            continue;
        }
        if (numberWords().contains(token)) {
            group += numberWords().value(token);
            sawNumber = true;
            continue;
        }
        if (token == QStringLiteral("hundred") && sawNumber) {
            group = (group == 0 ? 1 : group) * 100;
            continue;
        }
        if (token == QStringLiteral("thousand") && sawNumber) {
            total += (group == 0 ? 1 : group) * 1000;
            group = 0;
            continue;
        }
        if (token == QStringLiteral("million") && sawNumber) {
            total += (group == 0 ? 1 : group) * 1000000;
            group = 0;
            continue;
        }
        // Any other word ends the number. "three hundred calories a day" is
        // three hundred, not three hundred and something.
        if (sawNumber) {
            break;
        }
    }

    if (!sawNumber) {
        return {};
    }
    return static_cast<double>(total + group);
}

bool isAffirmative(const QString &text)
{
    static const QSet<QString> words{QStringLiteral("yes"),      QStringLiteral("yeah"),
                                     QStringLiteral("yep"),      QStringLiteral("yup"),
                                     QStringLiteral("sure"),     QStringLiteral("ok"),
                                     QStringLiteral("okay"),     QStringLiteral("confirm"),
                                     QStringLiteral("confirmed"), QStringLiteral("affirmative"),
                                     QStringLiteral("correct"),  QStringLiteral("right"),
                                     QStringLiteral("do"),       QStringLiteral("please")};
    const QStringList tokens{tokenize(text)};
    // The first few words only. "yes" buried at the end of a long sentence is
    // more likely to be part of a new request than an answer to the question.
    for (qsizetype index{0}; index < std::min<qsizetype>(tokens.size(), 3); ++index) {
        if (words.contains(tokens.at(index))) {
            return true;
        }
    }
    return false;
}

bool isNegative(const QString &text)
{
    static const QSet<QString> words{QStringLiteral("no"),     QStringLiteral("nope"),
                                     QStringLiteral("nah"),    QStringLiteral("cancel"),
                                     QStringLiteral("stop"),   QStringLiteral("forget"),
                                     QStringLiteral("abort"),  QStringLiteral("negative"),
                                     QStringLiteral("dont"),   QStringLiteral("never")};
    const QStringList tokens{tokenize(text)};
    for (qsizetype index{0}; index < std::min<qsizetype>(tokens.size(), 3); ++index) {
        if (words.contains(tokens.at(index))) {
            return true;
        }
    }
    return false;
}

IntentResolution matchIntent(const QString &utterance, const QList<IntentSpec> &intents)
{
    const QString normalized{normalizeUtterance(utterance)};
    IntentResolution best;
    int bestLiterals{-1};

    if (normalized.isEmpty()) {
        return best;
    }

    for (const IntentSpec &intent : intents) {
        for (const QString &phrase : intent.phrases) {
            QStringList names;
            const QRegularExpression expression{phraseToRegex(phrase, names)};
            if (!expression.isValid()) {
                continue;
            }
            const QRegularExpressionMatch match{expression.match(normalized)};
            if (!match.hasMatch()) {
                continue;
            }

            const int literals{literalWordsOf(phrase)};
            if (literals <= bestLiterals) {
                continue;
            }

            IntentResolution candidate;
            candidate.id = intent.id;
            for (const QString &slot : std::as_const(names)) {
                const QString captured{match.captured(slot)};
                if (!captured.isEmpty()) {
                    candidate.captures.insert(slot, refineSlot(captured));
                }
            }
            // A phrase that matched matched all of its literal words. The
            // confidence says how much of what was said the phrase accounts
            // for, which is what separates "add 300 calories" from a phrase
            // that happened to appear inside a much longer sentence.
            const qsizetype spoken{normalized.split(QLatin1Char{' '}, Qt::SkipEmptyParts).size()};
            candidate.confidence = spoken > 0
                                       ? std::min(1.0,
                                                  0.6
                                                      + (0.4 * literals
                                                         / static_cast<double>(spoken)))
                                       : 0.6;
            best = candidate;
            bestLiterals = literals;
        }
    }

    if (bestLiterals >= 0) {
        return best;
    }

    // Nothing matched a phrase. The descriptions are the fallback, and they are
    // scored rather than required, because a description is prose and the
    // speaker was not reading it out.
    double bestScore{descriptionFloor};
    for (const IntentSpec &intent : intents) {
        const QStringList words{contentWords(intent.description)};
        if (words.isEmpty()) {
            continue;
        }
        int found{0};
        for (const QString &word : words) {
            const QRegularExpression present{QStringLiteral("\\b") + QRegularExpression::escape(word)
                                             + QStringLiteral("\\b")};
            if (present.match(normalized).hasMatch()) {
                ++found;
            }
        }
        const double score{static_cast<double>(found) / words.size()};
        if (score >= bestScore) {
            bestScore = score;
            best = IntentResolution{};
            best.id = intent.id;
            best.confidence = score;
            const QVariant quantity{parseQuantity(normalized)};
            if (quantity.isValid()) {
                // The one slot a description-only trigger can still fill. A
                // request with a number in it almost always means that number,
                // and a skill that wants it under another name should write a
                // phrase.
                best.captures.insert(QStringLiteral("amount"), quantity);
            }
        }
    }

    return best;
}

} // namespace Kerfur
