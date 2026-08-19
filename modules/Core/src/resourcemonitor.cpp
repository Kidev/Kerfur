// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "resourcemonitor.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QList>
#include <QQmlEngine>
#include <QString>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace Kerfur {

namespace {

// A minute is often enough to draw a slope over a day and rare enough that the
// sampling itself never shows up in a profile.
constexpr int defaultSampleIntervalMs{60000};

} // namespace

ResourceMonitor::ResourceMonitor(QObject *parent) : QObject{parent}
{
    m_uptime.start();
    m_timer.setInterval(defaultSampleIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ResourceMonitor::sampleNow);
}

ResourceMonitor &ResourceMonitor::instance()
{
    static ResourceMonitor monitor;
    return monitor;
}

ResourceMonitor *ResourceMonitor::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

qint64 ResourceMonitor::residentSetSizeKb() const
{
    return m_residentSetSizeKb;
}

int ResourceMonitor::openFileDescriptors() const
{
    return m_openFileDescriptors;
}

int ResourceMonitor::threadCount() const
{
    return m_threadCount;
}

qint64 ResourceMonitor::uptimeSeconds() const
{
    return m_uptime.elapsed() / 1000;
}

void ResourceMonitor::registerQueue(QueueStatsSource *source)
{
    if (source == nullptr || m_queues.contains(source)) {
        return;
    }
    m_queues.append(source);
}

void ResourceMonitor::unregisterQueue(QueueStatsSource *source)
{
    m_queues.removeAll(source);
}

QList<QueueStats> ResourceMonitor::queueStats() const
{
    QList<QueueStats> stats;
    stats.reserve(m_queues.size());
    for (const QueueStatsSource *source : m_queues) {
        stats.append(source->queueStats());
    }
    return stats;
}

void ResourceMonitor::setSampleIntervalMs(int intervalMs)
{
    if (intervalMs <= 0) {
        m_timer.stop();
        return;
    }
    m_timer.setInterval(intervalMs);
    m_timer.start();
}

void ResourceMonitor::sampleNow()
{
    m_residentSetSizeKb = readResidentSetSizeKb();
    m_openFileDescriptors = countEntries(QStringLiteral("/proc/self/fd"));
    m_threadCount = countEntries(QStringLiteral("/proc/self/task"));

    QString queueSummary;
    for (const QueueStats &stats : queueStats()) {
        queueSummary += QStringLiteral(" %1=%2/%3(peak %4, dropped %5)")
                            .arg(stats.name)
                            .arg(stats.size)
                            .arg(stats.capacity)
                            .arg(stats.highWaterMark)
                            .arg(stats.dropped);
    }

    qInfo().noquote() << QStringLiteral("resources rss=%1kB fd=%2 threads=%3 uptime=%4s%5")
                             .arg(m_residentSetSizeKb)
                             .arg(m_openFileDescriptors)
                             .arg(m_threadCount)
                             .arg(uptimeSeconds())
                             .arg(queueSummary);

    emit sampled();
}

qint64 ResourceMonitor::readResidentSetSizeKb()
{
#if defined(Q_OS_LINUX)
    QFile file{QStringLiteral("/proc/self/statm")};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }

    // statm is "size resident shared text lib data dt", in pages. The second
    // field is what actually costs memory; the first is address space and grows
    // for reasons that are not leaks.
    const QList<QByteArray> fields{file.readLine().simplified().split(' ')};
    if (fields.size() < 2) {
        return -1;
    }

    bool parsed{false};
    const qint64 pages{fields.at(1).toLongLong(&parsed)};
    if (!parsed) {
        return -1;
    }

    const qint64 pageSizeKb{static_cast<qint64>(::sysconf(_SC_PAGESIZE)) / 1024};
    return pages * pageSizeKb;
#else
    return -1;
#endif
}

int ResourceMonitor::countEntries(const QString &directoryPath)
{
#if defined(Q_OS_LINUX)
    const QDir directory{directoryPath};
    if (!directory.exists()) {
        return -1;
    }
    return static_cast<int>(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
#else
    Q_UNUSED(directoryPath)
    return -1;
#endif
}

} // namespace Kerfur
