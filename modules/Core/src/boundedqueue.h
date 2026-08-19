// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_BOUNDEDQUEUE_H
#define KERFUR_BOUNDEDQUEUE_H

#include "queuestatssource.h"

#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include <deque>
#include <optional>
#include <utility>

namespace Kerfur {

/// A fixed capacity queue that drops its oldest element instead of growing.
///
/// Every producer and consumer pair in Kerfur uses one of these. The process has
/// to survive weeks of uptime, and the failure this rework exists to fix was a
/// buffer that grew whenever a producer outran its consumer, so nothing here is
/// allowed to grow with input rate.
///
/// Dropping the oldest rather than refusing the newest is deliberate. A
/// perception pipeline that cannot keep up should show the most recent view of
/// the world, not the head of a stale backlog.
///
/// Safe to use from several threads at once.
template<typename T>
class BoundedQueue final : public QueueStatsSource
{
public:
    /// A capacity below one is clamped to one rather than rejected, because a
    /// misconfigured capacity should degrade to a one deep queue, not to a
    /// queue that silently discards everything.
    BoundedQueue(qsizetype capacity, QString name)
        : m_name{std::move(name)}, m_capacity{capacity > 0 ? capacity : 1}
    {}

    ~BoundedQueue() override = default;

    /// Appends `value`, evicting the oldest element when already full. Returns
    /// true when something was evicted, so a caller that cares can log it.
    bool push(T value)
    {
        const QMutexLocker locker{&m_mutex};

        bool evicted{false};
        if (static_cast<qsizetype>(m_items.size()) >= m_capacity) {
            m_items.pop_front();
            ++m_dropped;
            evicted = true;
        }
        m_items.push_back(std::move(value));

        const qsizetype occupancy{static_cast<qsizetype>(m_items.size())};
        if (occupancy > m_highWaterMark) {
            m_highWaterMark = occupancy;
        }
        return evicted;
    }

    /// Removes and returns the oldest element, or nothing when empty.
    std::optional<T> pop()
    {
        const QMutexLocker locker{&m_mutex};

        if (m_items.empty()) {
            return std::nullopt;
        }
        std::optional<T> value{std::move(m_items.front())};
        m_items.pop_front();
        return value;
    }

    void clear()
    {
        const QMutexLocker locker{&m_mutex};
        m_items.clear();
    }

    QueueStats queueStats() const override
    {
        const QMutexLocker locker{&m_mutex};
        return QueueStats{m_name,
                          static_cast<qsizetype>(m_items.size()),
                          m_capacity,
                          m_highWaterMark,
                          m_dropped};
    }

private:
    mutable QMutex m_mutex;
    std::deque<T> m_items;
    QString m_name;
    qsizetype m_capacity;
    qsizetype m_highWaterMark{0};
    quint64 m_dropped{0};
};

} // namespace Kerfur

#endif // KERFUR_BOUNDEDQUEUE_H
