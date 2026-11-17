// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "session.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(sessionLog, "kerfur.core.session")

} // namespace

} // namespace Kerfur

#ifdef Q_OS_WASM

// Leaving QCoreApplication's event loop in a browser tears the runtime down
// under the page, which then shows a stopped application. The tab is closed
// instead, with Qt still running. `closed` turns true at once when the
// browser accepts.
// clang-format off
EM_JS(void, kerfurLeavePage, (), {
    window.close();
    if (!window.closed && window.history.length > 1)
        window.history.back();
});
// clang-format on

#endif

namespace Kerfur {

Session::Session(QObject *parent)
    : QObject{parent}
{}

QString Session::lightOption()
{
    return QStringLiteral("--light");
}

bool Session::lightIn(const QStringList &arguments)
{
    // The first entry is the program, which is never an option.
    return arguments.indexOf(lightOption(), 1) >= 0;
}

bool Session::light()
{
    static const bool value{[]() {
        const bool requested{lightIn(QCoreApplication::arguments())};
        if (requested) {
            qCInfo(sessionLog) << "light session: no models, no camera, no microphone";
        }
        return requested;
    }()};
    return value;
}

void Session::end()
{
#ifdef Q_OS_WASM
    kerfurLeavePage();
#else
    QCoreApplication::quit();
#endif
}

} // namespace Kerfur
