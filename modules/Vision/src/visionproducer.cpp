// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "visionproducer.h"

#include "cameraframesource.h"
#include "resourcemonitor.h"
#include "visionconfig.h"
#include "visioninferenceworker.h"
#include "visiontypes.h"
#include "worldmodel.h"

#include <QLoggingCategory>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(producerLog, "kerfur.vision")

// Two deep. One frame being worked on and one waiting is the whole useful
// backlog; anything more is latency nobody asked for.
constexpr qsizetype frameQueueDepth{2};
constexpr qsizetype perceptionQueueDepth{2};

} // namespace

VisionProducer::VisionProducer(WorldModel &world, QObject *parent)
    : QObject{parent}
    , m_world{world}
    , m_pipe{perceptionQueueDepth, QStringLiteral("vision.perception"), this}
    , m_frames{std::make_unique<BoundedQueue<VisionFrame>>(frameQueueDepth,
                                                          QStringLiteral("vision.frames"))}
{
    connect(&m_pipe, &PerceptionPipe::frameReady, &m_world, &WorldModel::applyPerceptionFrame);

    ResourceMonitor::instance().registerQueue(m_pipe.statsSource());
    ResourceMonitor::instance().registerQueue(m_frames.get());
}

VisionProducer::~VisionProducer()
{
    stopPipeline();

    // The queues die with this object, so the monitor must stop holding
    // pointers to them first.
    ResourceMonitor::instance().unregisterQueue(m_pipe.statsSource());
    ResourceMonitor::instance().unregisterQueue(m_frames.get());
}

void VisionProducer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    if (m_enabled) {
        startPipeline();
    } else {
        stopPipeline();
    }
    emit enabledChanged();
}

void VisionProducer::setOrientationOverride(int degrees)
{
    // Through a signal rather than a direct call, because the worker lives on
    // another thread and the backend is its private state.
    emit orientationOverrideRequested(degrees);
}

void VisionProducer::startPipeline()
{
    if (m_worker != nullptr) {
        return;
    }

    const VisionConfig config{VisionConfig::forPlatform()};

    m_source = new CameraFrameSource{*m_frames, this};
    m_source->setFrameStride(config.frameStride);

    // Parentless: it is moved to the worker thread, and a parent on the GUI
    // thread would make that move illegal.
    m_worker = new VisionInferenceWorker{*m_frames, m_pipe};
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_workerThread, &QThread::started, m_worker, &VisionInferenceWorker::initialize);
    connect(m_source,
            &CameraFrameSource::frameQueued,
            m_worker,
            &VisionInferenceWorker::processPending,
            Qt::QueuedConnection);
    connect(this,
            &VisionProducer::orientationOverrideRequested,
            m_worker,
            &VisionInferenceWorker::setOrientationOverride,
            Qt::QueuedConnection);
    connect(m_source, &CameraFrameSource::activeChanged, &m_world, &WorldModel::setCameraOnline);

    m_workerThread.setObjectName(QStringLiteral("kerfur.vision"));
    m_workerThread.start();
    m_source->start();
    qCInfo(producerLog) << "vision pipeline started";
}

void VisionProducer::stopPipeline()
{
    if (m_source != nullptr) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
    }

    if (m_workerThread.isRunning()) {
        m_workerThread.quit();
        m_workerThread.wait();
        // Deleted by the thread's finished signal, so this only drops the
        // pointer.
        m_worker = nullptr;
    } else if (m_worker != nullptr) {
        // The thread never ran, so nothing will emit finished and the worker
        // would otherwise be leaked. It is parentless by construction, because
        // a parent on this thread would have made the move illegal.
        delete m_worker;
        m_worker = nullptr;
    }

    m_frames->clear();
    m_world.setCameraOnline(false);
}

} // namespace Kerfur
