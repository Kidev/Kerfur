// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "settingspath.h"

#include <QDir>
#include <QUrl>

namespace Kerfur {

QString settingsDirectoryPath(const QString &value)
{
    if (value.isEmpty()) {
        return {};
    }

    // Only a value that actually carries the file scheme is converted. A
    // Windows path such as C:/models parses as a url whose scheme is "c", and
    // an unconditional round trip through QUrl is exactly what would corrupt
    // it.
    const QUrl url{value};
    if (url.isLocalFile()) {
        return QDir::cleanPath(url.toLocalFile());
    }

    return QDir::cleanPath(value);
}

} // namespace Kerfur
