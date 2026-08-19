// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "boundedqueue.h"
#include "resourcemonitor.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using Kerfur::BoundedQueue;
using Kerfur::QueueStats;
using Kerfur::ResourceMonitor;

class TestResourceMonitor : public QObject
{
    Q_OBJECT

private slots:
    void samplesTheProcessFootprint();
    void seesResidentMemoryGrow();
    void reportsRegisteredQueues();
    void forgetsAnUnregisteredQueue();
    void emitsSampledOnEverySample();
};

void TestResourceMonitor::samplesTheProcessFootprint()
{
#if !defined(Q_OS_LINUX)
    QSKIP("The process footprint is read from /proc, which only Linux has");
#endif
    ResourceMonitor monitor;
    monitor.sampleNow();

    QVERIFY(monitor.residentSetSizeKb() > 0);
    QVERIFY(monitor.openFileDescriptors() > 0);
    QVERIFY(monitor.threadCount() > 0);
    QVERIFY(monitor.uptimeSeconds() >= 0);
}

void TestResourceMonitor::seesResidentMemoryGrow()
{
#if !defined(Q_OS_LINUX)
    QSKIP("The process footprint is read from /proc, which only Linux has");
#endif
    ResourceMonitor monitor;
    monitor.sampleNow();
    const qint64 before{monitor.residentSetSizeKb()};

    // Touched, not just allocated: an untouched mapping never becomes resident,
    // so a monitor that watched virtual size instead would pass this while
    // being useless for catching a real leak. The pages are written through a
    // volatile pointer because the writes are otherwise a dead store into a
    // block nothing reads: an optimising build drops them, and with them the
    // page faults this test exists to observe.
    constexpr qsizetype blockSize{64 * 1024 * 1024};
    constexpr qsizetype pageStride{4096};
    std::unique_ptr<char[]> block{new char[blockSize]};
    volatile char *const pages{block.get()};
    for (qsizetype offset{0}; offset < blockSize; offset += pageStride) {
        pages[offset] = 1;
    }

    monitor.sampleNow();
    const qint64 after{monitor.residentSetSizeKb()};

    QVERIFY2(after - before > 32 * 1024,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before).arg(after)));
}

void TestResourceMonitor::reportsRegisteredQueues()
{
    ResourceMonitor monitor;
    BoundedQueue<int> queue{2, QStringLiteral("watched")};
    queue.push(1);
    queue.push(2);
    queue.push(3);

    monitor.registerQueue(&queue);
    const QList<QueueStats> stats{monitor.queueStats()};

    QCOMPARE(stats.size(), 1);
    QCOMPARE(stats.first().name, QStringLiteral("watched"));
    QCOMPARE(stats.first().dropped, 1U);
}

void TestResourceMonitor::forgetsAnUnregisteredQueue()
{
    ResourceMonitor monitor;
    BoundedQueue<int> queue{2, QStringLiteral("temporary")};
    monitor.registerQueue(&queue);
    monitor.unregisterQueue(&queue);

    // A queue that outlives its registration is a dangling pointer read on the
    // next sample, so unregistering has to actually remove it.
    QCOMPARE(monitor.queueStats().size(), 0);
}

void TestResourceMonitor::emitsSampledOnEverySample()
{
    ResourceMonitor monitor;
    QSignalSpy spy{&monitor, &ResourceMonitor::sampled};

    monitor.sampleNow();
    monitor.sampleNow();

    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(TestResourceMonitor)
#include "tst_resourcemonitor.moc"
