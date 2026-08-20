// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_IDENTITYDIRECTORY_H
#define KERFUR_IDENTITYDIRECTORY_H

#include <QString>
#include <QStringList>

namespace Kerfur {

/// Where the enrolled people live: an `ids.json` naming each of them, and the
/// face and voice files it points at.
///
/// Face recognition and speaker recognition resolve through here rather than
/// each carrying its own search order, because a person enrolled for one and
/// not the other is a configuration mistake that is far easier to see when both
/// halves are described by one file. What that file says is IdentityRegistry's
/// business; this is only the question of which directory it is in.
class IdentityDirectory
{
public:
    /// In order: the `Paths/identitiesPath` setting, the application data
    /// location, an `identities` directory beside the executable, one directly
    /// above it, and one under the working directory.
    static QStringList candidates();

    /// The first candidate that holds a manifest or at least one enrolled file,
    /// or empty. Existence alone is not enough: an empty `identities` directory
    /// left behind by an uninstall would otherwise mask a populated one further
    /// down the list, and report as "nobody is enrolled" rather than as "the
    /// wrong directory was found".
    static QString resolve();

    /// The candidates joined for a log line, so an empty gallery says where it
    /// looked.
    static QString describeSearchPath();

    /// The ids of every declared person. Reads the manifest, so this is the
    /// same list the two galleries enrol against.
    static QStringList enrolledNames(const QString &directory);

private:
    /// True when `directory` holds a manifest, or a face or voice file either
    /// directly or under `faces/` or `voices/`.
    static bool holdsIdentities(const QString &directory);
};

} // namespace Kerfur

#endif // KERFUR_IDENTITYDIRECTORY_H
