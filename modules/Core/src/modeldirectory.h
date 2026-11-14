// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_MODELDIRECTORY_H
#define KERFUR_MODELDIRECTORY_H

#include <QString>
#include <QStringList>

namespace Kerfur {

/// Where the provisioned inference model files live.
///
/// Kerfur boots offline, so nothing is downloaded at run time: `make models`
/// populates a directory once and this is the single definition of which
/// directories that may be. Vision's ModelCatalog and the face and speech
/// stacks that predate it all resolve through here, so a deployment has one
/// search order to satisfy rather than one per module.
class ModelDirectory
{
public:
    /// In order: the `Paths/modelsPath` setting that KSettings writes, the
    /// application data location, a `models` directory beside the executable,
    /// one directly above it (and in `Resources` on macOS), and on WebAssembly
    /// the preload mount point.
    static QStringList candidates();

    /// The first candidate that exists, or empty when none does. Only that one
    /// is ever searched afterwards, so a half populated override reads as
    /// missing models rather than being silently completed from elsewhere.
    static QString resolve();

    /// Absolute path to `fileName` inside the resolved directory, or empty when
    /// there is no directory or no such file in it.
    static QString locate(const QString &fileName);

    /// The candidates joined for an error message, so a missing model reports
    /// where it was looked for and not merely that it is absent.
    static QString describeSearchPath();
};

} // namespace Kerfur

#endif // KERFUR_MODELDIRECTORY_H
