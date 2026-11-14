// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ASSETSEARCH_H
#define KERFUR_ASSETSEARCH_H

#include <QString>
#include <QStringList>

namespace Kerfur {

/// Where a directory of user authored content is looked for.
///
/// Three such directories exist -- the models, the identities and the skills --
/// and every one of them has to be findable from an installed tree, from a
/// build tree and from a developer's override without any of the three knowing
/// about the others. The order is the same in all cases, so it is written once.
///
/// Deliberately not merged with ModelDirectory: models are provisioned by
/// `make models` and verified against a manifest, so a half populated override
/// there has to be visible as missing stages rather than quietly completed from
/// the next candidate down. These directories have no manifest and take the
/// first that answers.
namespace AssetSearch {

/// In order: the named setting, the application data location, a directory
/// beside the executable, one directly above it (and in `Resources` on macOS),
/// and one under the working directory.
///
/// The last two are what make a build tree work without configuration: running
/// `./build/kerfur` from a checkout puts the executable one level below the
/// content that is committed next to the source.
QStringList candidates(const QString &settingsKey, const QString &folderName);

/// The candidates joined for a log line, so an empty result can say where it
/// looked rather than only that it failed.
QString describe(const QString &settingsKey, const QString &folderName);

} // namespace AssetSearch

} // namespace Kerfur

#endif // KERFUR_ASSETSEARCH_H
