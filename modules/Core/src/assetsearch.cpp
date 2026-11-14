// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "assetsearch.h"

#include "settingspath.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace Kerfur::AssetSearch {

QStringList candidates(const QString &settingsKey, const QString &folderName)
{
    QStringList directories;

    if (!settingsKey.isEmpty()) {
        const QSettings settings;
        const QString configured{settingsDirectoryPath(settings.value(settingsKey).toString())};
        if (!configured.isEmpty()) {
            directories.append(configured);
        }
    }

    const QString appData{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    if (!appData.isEmpty()) {
        directories.append(appData + QLatin1Char('/') + folderName);
    }

    const QString executable{QCoreApplication::applicationDirPath()};
    if (!executable.isEmpty()) {
        directories.append(executable + QLatin1Char('/') + folderName);
        directories.append(
            QDir::cleanPath(executable + QStringLiteral("/../") + folderName));
#ifdef Q_OS_MACOS
        directories.append(
            QDir::cleanPath(executable + QStringLiteral("/../Resources/") + folderName));
#endif
    }

    directories.append(QDir::cleanPath(QDir::currentPath() + QLatin1Char('/') + folderName));

    directories.removeDuplicates();
    return directories;
}

QString describe(const QString &settingsKey, const QString &folderName)
{
    return candidates(settingsKey, folderName).join(QStringLiteral(", "));
}

} // namespace Kerfur::AssetSearch
