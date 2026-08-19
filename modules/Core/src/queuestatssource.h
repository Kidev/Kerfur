// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_QUEUESTATSSOURCE_H
#define KERFUR_QUEUESTATSSOURCE_H

#include <QString>

namespace Kerfur {

/// A snapshot of one queue's occupancy.
///
/// `highWaterMark` and `dropped` are the two numbers worth watching: a queue
/// pinned at its capacity with a climbing drop count means a consumer stopped
/// consuming, which is the shape the previous build's failure would have had if
/// anything had been measuring it.
struct QueueStats
{
    QString name;
    qsizetype size{0};
    qsizetype capacity{0};
    qsizetype highWaterMark{0};
    quint64 dropped{0};
};

/// Implemented by anything ResourceMonitor reports occupancy for.
///
/// This exists so the monitor can hold a list of queues without knowing any of
/// their element types, and so BoundedQueue does not have to be a QObject:
/// queues are created on worker threads and shared across them, which is not
/// what QObject affinity is for.
class QueueStatsSource
{
public:
    QueueStatsSource() = default;
    virtual ~QueueStatsSource();

    QueueStatsSource(const QueueStatsSource &) = delete;
    QueueStatsSource &operator=(const QueueStatsSource &) = delete;

    virtual QueueStats queueStats() const = 0;
};

} // namespace Kerfur

#endif // KERFUR_QUEUESTATSSOURCE_H
