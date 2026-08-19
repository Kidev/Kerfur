// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_RESOURCEMONITOR_H
#define KERFUR_RESOURCEMONITOR_H

#include "queuestatssource.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Watches the process's own footprint and every registered queue.
///
/// The build this replaces died after about a day with nothing recorded about
/// how it got there. This exists so that growth is a slope on a graph somebody
/// already looks at, well before it is an out of memory kill at three in the
/// morning. P4 publishes these as Home Assistant diagnostic entities.
class ResourceMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qint64 residentSetSizeKb READ residentSetSizeKb NOTIFY sampled)
    Q_PROPERTY(int openFileDescriptors READ openFileDescriptors NOTIFY sampled)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY sampled)
    Q_PROPERTY(qint64 uptimeSeconds READ uptimeSeconds NOTIFY sampled)

public:
    explicit ResourceMonitor(QObject *parent = nullptr);

    static ResourceMonitor &instance();
    static ResourceMonitor *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    qint64 residentSetSizeKb() const;
    int openFileDescriptors() const;
    int threadCount() const;
    qint64 uptimeSeconds() const;

    /// The caller keeps ownership and must unregister before the queue dies.
    /// Registration deliberately does not take ownership: queues are members of
    /// the workers that use them, and outliving that worker is the bug.
    void registerQueue(QueueStatsSource *source);
    void unregisterQueue(QueueStatsSource *source);

    QList<QueueStats> queueStats() const;

    /// A zero or negative interval stops periodic sampling.
    void setSampleIntervalMs(int intervalMs);

    Q_INVOKABLE void sampleNow();

signals:
    void sampled();

private:
    static qint64 readResidentSetSizeKb();
    static int countEntries(const QString &directoryPath);

    QTimer m_timer;
    QElapsedTimer m_uptime;
    QList<QueueStatsSource *> m_queues;
    qint64 m_residentSetSizeKb{-1};
    int m_openFileDescriptors{-1};
    int m_threadCount{-1};
};

} // namespace Kerfur

#endif // KERFUR_RESOURCEMONITOR_H
