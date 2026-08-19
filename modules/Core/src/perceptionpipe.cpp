// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptionpipe.h"

#include <QMetaObject>

#include <optional>
#include <utility>

namespace Kerfur {

PerceptionPipe::PerceptionPipe(qsizetype capacity, const QString &name, QObject *parent)
    : QObject{parent}, m_queue{capacity, name}
{
    ensurePerceptionMetaTypesRegistered();
}

void PerceptionPipe::submit(PerceptionFrame frame)
{
    m_queue.push(std::move(frame));

    // Queued, so drain runs on the thread that owns this object. That is the
    // only thread allowed to touch whatever frameReady is connected to, and it
    // is why submit is safe to call from an inference worker.
    QMetaObject::invokeMethod(this, &PerceptionPipe::drain, Qt::QueuedConnection);
}

QueueStatsSource *PerceptionPipe::statsSource()
{
    return &m_queue;
}

void PerceptionPipe::drain()
{
    // Drains everything rather than one per invocation: submit posts one event
    // per frame, but frames that arrived while an earlier drain was running
    // would otherwise wait for an event that has already been consumed.
    while (std::optional<PerceptionFrame> frame{m_queue.pop()}) {
        emit frameReady(*frame);
    }
}

} // namespace Kerfur
