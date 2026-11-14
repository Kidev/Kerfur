// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "cameraselection.h"

#include <QHash>

namespace Kerfur {

namespace {

qsizetype indexOfId(const QList<CameraCandidate> &cameras, const QByteArray &id)
{
    if (id.isEmpty()) {
        return -1;
    }
    for (qsizetype i{0}; i < cameras.size(); ++i) {
        if (cameras.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

} // namespace

qsizetype chooseCamera(const QList<CameraCandidate> &cameras,
                       const QByteArray &wanted,
                       const QByteArray &current,
                       const QByteArray &systemDefault)
{
    if (cameras.isEmpty()) {
        return -1;
    }

    const qsizetype wantedIndex{indexOfId(cameras, wanted)};
    if (wantedIndex >= 0) {
        return wantedIndex;
    }

    const qsizetype currentIndex{indexOfId(cameras, current)};
    if (currentIndex >= 0) {
        return currentIndex;
    }

    for (qsizetype i{0}; i < cameras.size(); ++i) {
        if (cameras.at(i).frontFacing) {
            return i;
        }
    }

    const qsizetype defaultIndex{indexOfId(cameras, systemDefault)};
    return defaultIndex >= 0 ? defaultIndex : 0;
}

QStringList cameraLabels(const QList<CameraCandidate> &cameras)
{
    QStringList labels;
    labels.reserve(cameras.size());
    QHash<QString, int> seen;

    for (qsizetype i{0}; i < cameras.size(); ++i) {
        const QString base{cameras.at(i).description.trimmed().isEmpty()
                               ? QStringLiteral("Camera %1").arg(i + 1)
                               : cameras.at(i).description.trimmed()};
        const int count{++seen[base]};
        labels.append(count == 1 ? base : QStringLiteral("%1 (%2)").arg(base).arg(count));
    }
    return labels;
}

} // namespace Kerfur
