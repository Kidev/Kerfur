// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_IDENTITYDIRECTORY_H
#define KERFUR_IDENTITYDIRECTORY_H

#include <QString>
#include <QStringList>

namespace Kerfur {

/// Where the enrolled people live: one `name.png` per face and one `name.mp3`
/// per voice, the shared stem being what makes them the same person.
///
/// Face recognition and speaker recognition resolve through here rather than
/// each carrying its own search order, because a person enrolled for one and
/// not the other is a configuration mistake that is far easier to see when both
/// halves are required to sit in one directory.
class IdentityDirectory
{
public:
    /// In order: the `Paths/identitiesPath` setting, the application data
    /// location, an `identities` directory beside the executable, one directly
    /// above it, and one under the working directory.
    ///
    /// The last two are what make a build tree work without configuration:
    /// running `./build/kerfur` from a checkout puts the executable one level
    /// below the gallery that is committed next to the source.
    static QStringList candidates();

    /// The first candidate that exists and holds at least one enrolled file, or
    /// empty. Existence alone is not enough: an empty `identities` directory
    /// left behind by an uninstall would otherwise mask a populated one further
    /// down the list, and report as "nobody is enrolled" rather than as "the
    /// wrong directory was found".
    static QString resolve();

    /// The candidates joined for a log line, so an empty gallery says where it
    /// looked.
    static QString describeSearchPath();

    /// The stems of every enrolled person, from either half of the pair.
    static QStringList enrolledNames(const QString &directory);
};

} // namespace Kerfur

#endif // KERFUR_IDENTITYDIRECTORY_H
