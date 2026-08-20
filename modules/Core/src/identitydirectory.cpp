// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitydirectory.h"

#include "settingspath.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

namespace Kerfur {

namespace {

const QStringList &galleryFilters()
{
    // Both halves of an enrolment. A directory holding only faces is still a
    // gallery, and so is one holding only voices.
    static const QStringList filters{QStringLiteral("*.png"),
                                     QStringLiteral("*.jpg"),
                                     QStringLiteral("*.jpeg"),
                                     QStringLiteral("*.bmp"),
                                     QStringLiteral("*.mp3"),
                                     QStringLiteral("*.wav"),
                                     QStringLiteral("*.flac"),
                                     QStringLiteral("*.ogg"),
                                     QStringLiteral("*.m4a")};
    return filters;
}

} // namespace

QStringList IdentityDirectory::candidates()
{
    QStringList directories;

    const QSettings settings;
    const QString configured{
        settingsDirectoryPath(settings.value(QStringLiteral("Paths/identitiesPath")).toString())};
    if (!configured.isEmpty()) {
        directories.append(configured);
    }

    const QString appData{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    if (!appData.isEmpty()) {
        directories.append(appData + QStringLiteral("/identities"));
    }

    const QString executable{QCoreApplication::applicationDirPath()};
    if (!executable.isEmpty()) {
        directories.append(executable + QStringLiteral("/identities"));
        directories.append(QDir::cleanPath(executable + QStringLiteral("/../identities")));
    }

    directories.append(QDir::cleanPath(QDir::currentPath() + QStringLiteral("/identities")));

    directories.removeDuplicates();
    return directories;
}

QString IdentityDirectory::resolve()
{
    for (const QString &candidate : candidates()) {
        const QDir directory{candidate};
        if (!directory.exists()) {
            continue;
        }
        if (directory.entryInfoList(galleryFilters(), QDir::Files | QDir::Readable).isEmpty()) {
            continue;
        }
        return directory.absolutePath();
    }
    return {};
}

QString IdentityDirectory::describeSearchPath()
{
    return candidates().join(QStringLiteral(", "));
}

QStringList IdentityDirectory::enrolledNames(const QString &directory)
{
    if (directory.isEmpty()) {
        return {};
    }

    const QDir gallery{directory};
    QSet<QString> stems;
    const QFileInfoList entries{
        gallery.entryInfoList(galleryFilters(), QDir::Files | QDir::Readable, QDir::Name)};
    for (const QFileInfo &entry : entries) {
        stems.insert(entry.completeBaseName());
    }

    QStringList names{stems.values()};
    names.sort();
    return names;
}

} // namespace Kerfur
