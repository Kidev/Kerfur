// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "modeldirectory.h"

#include "settingspath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace Kerfur {

QStringList ModelDirectory::candidates()
{
    QStringList directories;

    // KSettings owns this key, so a control panel override is honoured without
    // any module having to import anything from the QML layer.
    const QSettings settings;
    const QString configured{
        settingsDirectoryPath(settings.value(QStringLiteral("Paths/modelsPath")).toString())};
    if (!configured.isEmpty()) {
        directories.append(configured);
    }

    const QString appData{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    if (!appData.isEmpty()) {
        directories.append(appData + QStringLiteral("/models"));
    }

    directories.append(QCoreApplication::applicationDirPath() + QStringLiteral("/models"));

#ifdef Q_OS_WASM
    // Emscripten's virtual file system, where the link time preload rule mounts
    // the model directory. There is no writable location worth searching in a
    // browser, so this is the one that actually resolves.
    directories.append(QStringLiteral("/models"));
#endif

    // The configured override is usually the application data location spelled
    // differently, and a message that lists the same directory twice reads as a
    // bug in the message.
    directories.removeDuplicates();
    return directories;
}

QString ModelDirectory::resolve()
{
    for (const QString &candidate : candidates()) {
        const QDir directory{candidate};
        if (directory.exists()) {
            return directory.absolutePath();
        }
    }
    return {};
}

QString ModelDirectory::locate(const QString &fileName)
{
    const QString directory{resolve()};
    if (directory.isEmpty() || fileName.isEmpty()) {
        return {};
    }

    const QString absolute{QDir{directory}.absoluteFilePath(fileName)};
    return QFileInfo::exists(absolute) ? absolute : QString{};
}

QString ModelDirectory::describeSearchPath()
{
    return candidates().join(QStringLiteral(", "));
}

} // namespace Kerfur
