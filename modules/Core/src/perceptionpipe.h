// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PERCEPTIONPIPE_H
#define KERFUR_PERCEPTIONPIPE_H

#include "boundedqueue.h"
#include "perceptiontypes.h"

#include <QObject>
#include <QString>

namespace Kerfur {

/// Carries perception frames from a worker thread to the thread that owns this
/// object, dropping the oldest rather than queueing without bound.
///
/// This is the only sanctioned way into WorldModel from a producer. An inference
/// worker that outruns the GUI thread is normal and expected, and what should
/// happen then is that intermediate frames are discarded, not that they pile up
/// until the process is killed.
class PerceptionPipe : public QObject
{
    Q_OBJECT

public:
    PerceptionPipe(qsizetype capacity, const QString &name, QObject *parent = nullptr);

    /// Callable from any thread. Returns immediately; delivery happens later on
    /// the owning thread.
    void submit(PerceptionFrame frame);

    QueueStatsSource *statsSource();

signals:
    void frameReady(const Kerfur::PerceptionFrame &frame);

private:
    void drain();

    BoundedQueue<PerceptionFrame> m_queue;
};

} // namespace Kerfur

#endif // KERFUR_PERCEPTIONPIPE_H
