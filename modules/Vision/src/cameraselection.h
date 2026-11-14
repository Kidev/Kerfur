// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CAMERASELECTION_H
#define KERFUR_CAMERASELECTION_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace Kerfur {

/// What the choice needs to know about one video input. A QCameraDevice cannot
/// be built outside Qt Multimedia's backends, so the rule works on this instead
/// and stays testable without a camera.
struct CameraCandidate
{
    QByteArray id;
    QString description;
    bool frontFacing{false};
};

/// The index of the camera to open, or -1 when there is none.
///
/// `wanted` is the persisted choice and wins whenever it is plugged in. When it
/// is empty or absent, the camera already open (`current`) is kept, so that a
/// device list refresh never swaps the picture under the user; failing that a
/// front facing camera, then the system default, then the first one listed.
qsizetype chooseCamera(const QList<CameraCandidate> &cameras,
                       const QByteArray &wanted,
                       const QByteArray &current,
                       const QByteArray &systemDefault);

/// One label per camera, in the same order, each distinct. Two identical
/// webcams report the same description, and a drop down with two equal rows
/// cannot say which one is open, so later duplicates get " (2)", " (3)".
QStringList cameraLabels(const QList<CameraCandidate> &cameras);

} // namespace Kerfur

#endif // KERFUR_CAMERASELECTION_H
