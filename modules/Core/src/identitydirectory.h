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
/// Nothing ships with the application. People are taught at run time and kept
/// in `writableDirectory()`; the one other gallery is a directory named by hand
/// in the `Paths/identitiesPath` setting, which is unset by default. A source
/// checkout or an install tree is never searched, so a build carries nobody's
/// face or voice.
///
/// Face recognition and speaker recognition resolve through here rather than
/// each carrying its own search order, because a person enrolled for one and
/// not the other is a configuration mistake that is far easier to see when both
/// halves are described by one file. What that file says is IdentityRegistry's
/// business; this is only the question of which directory it is in.
class IdentityDirectory
{
public:
    /// In order: the `Paths/identitiesPath` setting when it is set, then
    /// `writableDirectory()`.
    static QStringList candidates();

    /// The first candidate that holds a manifest or at least one enrolled file,
    /// or empty. Existence alone is not enough: an empty configured directory
    /// would otherwise mask the populated store behind it, and report as
    /// "nobody is enrolled" rather than as "the wrong directory was found".
    static QString resolve();

    /// The candidates joined for a log line, so an empty gallery says where it
    /// looked.
    static QString describeSearchPath();

    /// Where people enrolled at run time are written: `identities` under the
    /// application data location. On the web build that directory is backed by
    /// the browser's IndexedDB, so it survives a reload.
    ///
    /// It is outside the source tree on purpose. A repository can then be
    /// shared without anybody's face or voice in it.
    static QString writableDirectory();

    /// Every gallery to enrol from, in merge order: the configured directory
    /// when it holds identities, then the writable directory when it holds
    /// any. Empty on a fresh installation. Feed this to
    /// `IdentityRegistry::load`, so a person taught at run time overrides a
    /// configured record of the same id.
    static QStringList galleries();

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
