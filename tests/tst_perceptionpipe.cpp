// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptionpipe.h"
#include "perceptiontypes.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

using Kerfur::PerceptionFrame;
using Kerfur::PerceptionPipe;

static PerceptionFrame makeFrame(quint64 sequence)
{
    PerceptionFrame frame;
    frame.setSequence(sequence);
    return frame;
}

class TestPerceptionPipe : public QObject
{
    Q_OBJECT

private slots:
    void deliversASubmittedFrame();
    void dropsTheOldestWhenTheConsumerCannotKeepUp();
    void deliversFromAnotherThreadOnTheOwningThread();
    void reportsItsQueueStatistics();
};

void TestPerceptionPipe::deliversASubmittedFrame()
{
    PerceptionPipe pipe{4, QStringLiteral("deliver")};
    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    pipe.submit(makeFrame(1));
    QVERIFY(spy.wait(2000));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<PerceptionFrame>().sequence(), 1U);
}

void TestPerceptionPipe::dropsTheOldestWhenTheConsumerCannotKeepUp()
{
    PerceptionPipe pipe{2, QStringLiteral("drop")};
    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    // Nothing drains while this loop runs, because the drain is queued and the
    // event loop is not running yet. Five frames into a two deep queue.
    for (quint64 sequence{1}; sequence <= 5; ++sequence) {
        pipe.submit(makeFrame(sequence));
    }
    QCoreApplication::processEvents();

    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).value<PerceptionFrame>().sequence(), 4U);
    QCOMPARE(spy.at(1).at(0).value<PerceptionFrame>().sequence(), 5U);
}

void TestPerceptionPipe::deliversFromAnotherThreadOnTheOwningThread()
{
    PerceptionPipe pipe{8, QStringLiteral("cross")};

    QThread *owningThread{QThread::currentThread()};
    QThread *observed{nullptr};
    connect(&pipe, &PerceptionPipe::frameReady, this, [&observed]() {
        observed = QThread::currentThread();
    });

    QSignalSpy spy{&pipe, &PerceptionPipe::frameReady};

    QThread *producer{QThread::create([&pipe]() { pipe.submit(makeFrame(42)); })};
    producer->start();
    QVERIFY(producer->wait(5000));
    delete producer;

    QVERIFY(spy.wait(2000));
    QCOMPARE(observed, owningThread);
}

void TestPerceptionPipe::reportsItsQueueStatistics()
{
    PerceptionPipe pipe{2, QStringLiteral("stats")};
    for (quint64 sequence{1}; sequence <= 5; ++sequence) {
        pipe.submit(makeFrame(sequence));
    }

    const Kerfur::QueueStats stats{pipe.statsSource()->queueStats()};
    QCOMPARE(stats.name, QStringLiteral("stats"));
    QCOMPARE(stats.capacity, 2);
    QCOMPARE(stats.dropped, 3U);
}

QTEST_GUILESS_MAIN(TestPerceptionPipe)
#include "tst_perceptionpipe.moc"
