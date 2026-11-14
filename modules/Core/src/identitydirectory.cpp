// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitydirectory.h"

#include "identityregistry.h"
#include "settingspath.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace Kerfur {

namespace {

constexpr QLatin1StringView settingsKey{"Paths/identitiesPath"};
constexpr QLatin1StringView folderName{"identities"};

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

bool holdsFiles(const QDir &directory)
{
    return directory.exists()
           && !directory.entryInfoList(galleryFilters(), QDir::Files | QDir::Readable).isEmpty();
}

} // namespace

QStringList IdentityDirectory::candidates()
{
    QStringList directories;

    const QSettings settings;
    const QString configured{settingsDirectoryPath(settings.value(QString{settingsKey}).toString())};
    if (!configured.isEmpty()) {
        directories.append(configured);
    }

    const QString writable{writableDirectory()};
    if (!writable.isEmpty() && !directories.contains(writable)) {
        directories.append(writable);
    }
    return directories;
}

bool IdentityDirectory::holdsIdentities(const QString &directory)
{
    const QDir gallery{directory};
    if (!gallery.exists()) {
        return false;
    }
    // The manifest first. A gallery whose faces all live under `faces/` has
    // nothing at its own level, and the version of this test that only looked
    // at the top level declared such a directory empty and walked past it.
    if (QFileInfo::exists(gallery.absoluteFilePath(IdentityRegistry::manifestFileName()))) {
        return true;
    }
    return holdsFiles(gallery)
           || holdsFiles(QDir{gallery.absoluteFilePath(QStringLiteral("faces"))})
           || holdsFiles(QDir{gallery.absoluteFilePath(QStringLiteral("voices"))});
}

QString IdentityDirectory::resolve()
{
    for (const QString &candidate : candidates()) {
        if (holdsIdentities(candidate)) {
            return QDir{candidate}.absolutePath();
        }
    }
    return {};
}

QString IdentityDirectory::writableDirectory()
{
    const QString appData{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    if (appData.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(appData + QLatin1Char('/') + folderName);
}

QStringList IdentityDirectory::galleries()
{
    const QString writable{writableDirectory()};

    QStringList directories;
    for (const QString &candidate : candidates()) {
        const QString absolute{QDir{candidate}.absolutePath()};
        if (!writable.isEmpty() && absolute == writable) {
            continue;
        }
        if (holdsIdentities(absolute)) {
            directories.append(absolute);
            break;
        }
    }
    if (!writable.isEmpty() && holdsIdentities(writable)) {
        directories.append(writable);
    }
    return directories;
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
    return IdentityRegistry::load(directory).names();
}

} // namespace Kerfur
