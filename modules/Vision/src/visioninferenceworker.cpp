// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "visioninferenceworker.h"

#include "modelcatalog.h"
#include "perceptionpipe.h"
#include "settingspath.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

#include <optional>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(workerLog, "kerfur.vision.worker")

QString galleryDirectory()
{
    const QSettings settings;
    const QString configured{
        settingsDirectoryPath(settings.value(QStringLiteral("Paths/facesGalleryPath")).toString())};
    if (!configured.isEmpty()) {
        return configured;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/faces");
}

QString identityCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/identities.json");
}

} // namespace

VisionInferenceWorker::VisionInferenceWorker(BoundedQueue<VisionFrame> &queue,
                                             PerceptionPipe &pipe,
                                             QObject *parent)
    : QObject{parent}, m_queue{queue}, m_pipe{pipe}
{}

void VisionInferenceWorker::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    const ModelCatalog catalog{ModelCatalog::resolve()};
    if (catalog.directory().isEmpty()) {
        qCWarning(workerLog) << "no model directory found; perception stays idle";
        return;
    }
    qCInfo(workerLog) << "models from" << catalog.directory();

    const bool ready{m_backend.initialize(catalog, galleryDirectory(), identityCachePath())};
    if (ready != m_ready) {
        m_ready = ready;
        emit readyChanged(m_ready);
    }
}

void VisionInferenceWorker::processPending()
{
    if (!m_ready) {
        m_queue.clear();
        return;
    }

    // Drain to the newest. Everything behind it is a view of the world that has
    // already been superseded, and processing it would put the pipeline further
    // behind on every frame rather than catching it up.
    std::optional<VisionFrame> newest;
    while (std::optional<VisionFrame> frame{m_queue.pop()}) {
        newest = std::move(frame);
    }
    if (!newest.has_value()) {
        return;
    }

    m_pipe.submit(m_backend.process(*newest));
}

void VisionInferenceWorker::setOrientationOverride(int degrees)
{
    m_backend.setOrientationOverride(degrees);
}

} // namespace Kerfur
