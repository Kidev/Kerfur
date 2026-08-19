// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SETTINGSPATH_H
#define KERFUR_SETTINGSPATH_H

#include <QString>

namespace Kerfur {

/// Turns a directory setting into a filesystem path.
///
/// QML's StandardPaths returns urls, so every path KSettings derives from it
/// reaches C++ as `file:///...`, which QDir does not accept and which therefore
/// resolves to nothing at all. This is where the two representations meet, so
/// that a hand written plain path in the configuration file works just as well
/// as the one the settings layer writes by itself.
QString settingsDirectoryPath(const QString &value);

} // namespace Kerfur

#endif // KERFUR_SETTINGSPATH_H
