// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_LANGUAGEBACKEND_H
#define KERFUR_LANGUAGEBACKEND_H

#include "intent.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kerfur {

/// One question put to whatever understands language.
///
/// Everything the answer could possibly need is in here, because the backend is
/// allowed to be a process on another machine. It gets the sentence, who said
/// it, the whole catalogue of what the loaded skills can do, and the current
/// value of every variable -- which is what lets a model answer "am I over my
/// limit" without a round trip to ask.
struct LanguageRequest
{
    QString utterance;
    QString speaker;

    /// Rank of the speaker, so a backend that wants to phrase an answer
    /// differently for the person it works for can.
    int priority{0};

    QList<IntentSpec> intents;
    QVariantMap variables;

    /// Matches the resolution back to the request. Requests may be answered out
    /// of order, and a backend that took two seconds over one sentence must not
    /// have its answer applied to the next.
    quint64 token{0};
};

/// The answer, whoever produced it.
struct LanguageResolution
{
    quint64 token{0};
    IntentResolution intent;
};

/// What turns a sentence into an intent.
///
/// The whole point of this class is that it has exactly one implementation
/// today and is written as though it had several. Kerfur has no language model
/// yet; when it has one, it becomes a subclass that posts the same request to
/// an endpoint and emits the same resolution, and nothing above it changes --
/// not the skills, not the confirmation flow, not the variables. The seam is
/// asynchronous for that reason alone: the pattern matcher answers within a
/// microsecond and does not need to be, but a model does, and a seam that has
/// to be widened later is a seam that was drawn in the wrong place.
class LanguageBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~LanguageBackend() override = default;

    /// For the panel, so that which one is in use is visible rather than
    /// inferred from how good the answers are.
    virtual QString name() const = 0;

    /// Must eventually emit `resolved` with the same token, exactly once,
    /// including when it understood nothing. A request that is answered with
    /// silence leaves the registry holding a pending utterance for ever.
    virtual void resolve(const LanguageRequest &request) = 0;

signals:
    void resolved(const Kerfur::LanguageResolution &resolution);
};

/// The backend that ships: phrases and descriptions, matched locally.
///
/// It is not a placeholder. It runs offline, costs nothing, and is exactly
/// right for the commands a robot face is actually given -- which are short,
/// repetitive and written down in advance by the person who will say them.
class PatternLanguageBackend : public LanguageBackend
{
    Q_OBJECT

public:
    using LanguageBackend::LanguageBackend;

    QString name() const override;
    void resolve(const LanguageRequest &request) override;
};

} // namespace Kerfur

Q_DECLARE_METATYPE(Kerfur::LanguageResolution)

#endif // KERFUR_LANGUAGEBACKEND_H
