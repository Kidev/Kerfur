// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "boundedqueue.h"

#include <QTest>
#include <QThread>

#include <optional>

using Kerfur::BoundedQueue;
using Kerfur::QueueStats;

class TestBoundedQueue : public QObject
{
    Q_OBJECT

private slots:
    void popsInOrder();
    void popsNothingWhenEmpty();
    void evictsTheOldestWhenFull();
    void reportsHighWaterMarkAndDrops();
    void clampsCapacityToAtLeastOne();
    void survivesConcurrentProducers();
};

void TestBoundedQueue::popsInOrder()
{
    BoundedQueue<int> queue{4, QStringLiteral("order")};
    QVERIFY(!queue.push(1));
    QVERIFY(!queue.push(2));
    QCOMPARE(queue.pop().value(), 1);
    QCOMPARE(queue.pop().value(), 2);
}

void TestBoundedQueue::popsNothingWhenEmpty()
{
    BoundedQueue<int> queue{4, QStringLiteral("empty")};
    QVERIFY(!queue.pop().has_value());
}

void TestBoundedQueue::evictsTheOldestWhenFull()
{
    BoundedQueue<int> queue{2, QStringLiteral("evict")};
    QVERIFY(!queue.push(1));
    QVERIFY(!queue.push(2));
    QVERIFY(queue.push(3));

    // 1 is gone rather than 3 being refused: the newest view of the world is
    // the one worth keeping.
    QCOMPARE(queue.pop().value(), 2);
    QCOMPARE(queue.pop().value(), 3);
    QVERIFY(!queue.pop().has_value());
}

void TestBoundedQueue::reportsHighWaterMarkAndDrops()
{
    BoundedQueue<int> queue{2, QStringLiteral("stats")};
    queue.push(1);
    queue.push(2);
    queue.push(3);
    queue.push(4);

    const QueueStats stats{queue.queueStats()};
    QCOMPARE(stats.name, QStringLiteral("stats"));
    QCOMPARE(stats.size, 2);
    QCOMPARE(stats.capacity, 2);
    QCOMPARE(stats.highWaterMark, 2);
    QCOMPARE(stats.dropped, 2U);
}

void TestBoundedQueue::clampsCapacityToAtLeastOne()
{
    BoundedQueue<int> queue{0, QStringLiteral("zero")};
    queue.push(7);
    QCOMPARE(queue.queueStats().capacity, 1);
    QCOMPARE(queue.pop().value(), 7);
}

void TestBoundedQueue::survivesConcurrentProducers()
{
    BoundedQueue<int> queue{8, QStringLiteral("threads")};

    QThread *producer{QThread::create([&queue]() {
        for (int index{0}; index < 5000; ++index) {
            queue.push(index);
        }
    })};
    producer->start();

    for (int index{0}; index < 5000; ++index) {
        queue.push(index);
    }
    QVERIFY(producer->wait(30000));
    delete producer;

    // The only invariant that matters under contention: it never exceeded its
    // capacity, which is what "cannot grow" means.
    QVERIFY(queue.queueStats().size <= 8);
    QVERIFY(queue.queueStats().highWaterMark <= 8);
}

QTEST_APPLESS_MAIN(TestBoundedQueue)
#include "tst_boundedqueue.moc"
