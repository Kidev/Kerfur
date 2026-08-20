// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "languagebackend.h"

namespace Kerfur {

QString PatternLanguageBackend::name() const
{
    return QStringLiteral("patterns");
}

void PatternLanguageBackend::resolve(const LanguageRequest &request)
{
    LanguageResolution resolution;
    resolution.token = request.token;
    resolution.intent = matchIntent(request.utterance, request.intents);

    // Emitted rather than returned even though it is already computed. The
    // contract is asynchronous, and a caller written against a synchronous
    // return here would have to be rewritten the day the backend is a network
    // call, which is the day rewriting it is hardest.
    emit resolved(resolution);
}

} // namespace Kerfur
