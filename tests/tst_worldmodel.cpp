// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"
#include "worldmodel.h"

#include <QSignalSpy>
#include <QTest>

using Kerfur::PerceptionFrame;
using Kerfur::TrackedPerson;
using Kerfur::WorldModel;

static TrackedPerson makePerson(int trackId, qreal proximity, const QPointF &head)
{
    TrackedPerson person;
    person.setTrackId(trackId);
    person.setProximity(proximity);
    person.setHeadCenter(head);
    return person;
}

class TestWorldModel : public QObject
{
    Q_OBJECT

private slots:
    void startsWithNobodyPresent();
    void reportsPresenceAndCount();
    void picksTheNearestPersonByProximity();
    void emitsPerceptionChangedOncePerFrame();
    void clearsTheNearestPersonWhenEverybodyLeaves();
    void onlyNotifiesHeardTextOnAChange();
    void tracksCameraAndMicrophoneAvailability();
    void notifiesWhenAFrameChangesCameraOrientation();
};

void TestWorldModel::startsWithNobodyPresent()
{
    WorldModel model;
    QCOMPARE(model.personCount(), 0);
    QVERIFY(!model.isPersonPresent());
    QVERIFY(!model.hasNearestPerson());
    QVERIFY(model.nearestHeadCenter().isNull());
}

void TestWorldModel::reportsPresenceAndCount()
{
    WorldModel model;
    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3}), makePerson(2, 0.5, QPointF{0.6, 0.35})});
    model.applyPerceptionFrame(frame);

    QCOMPARE(model.personCount(), 2);
    QVERIFY(model.isPersonPresent());
    QCOMPARE(model.people()->rowCount(), 2);
}

void TestWorldModel::picksTheNearestPersonByProximity()
{
    WorldModel model;
    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3}),
                     makePerson(2, 0.5, QPointF{0.6, 0.35}),
                     makePerson(3, 0.35, QPointF{0.1, 0.9})});
    model.applyPerceptionFrame(frame);

    QVERIFY(model.hasNearestPerson());
    QCOMPARE(model.nearestPerson().trackId(), 2);
    QCOMPARE(model.nearestHeadCenter(), QPointF(0.6, 0.35));
}

void TestWorldModel::emitsPerceptionChangedOncePerFrame()
{
    WorldModel model;
    QSignalSpy spy{&model, &WorldModel::perceptionChanged};

    PerceptionFrame frame;
    frame.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3})});
    model.applyPerceptionFrame(frame);

    // One frame is one notification. The build this replaces emitted five
    // separate property signals per frame, so consumers saw torn state.
    QCOMPARE(spy.count(), 1);
}

void TestWorldModel::clearsTheNearestPersonWhenEverybodyLeaves()
{
    WorldModel model;
    PerceptionFrame occupied;
    occupied.setPeople({makePerson(1, 0.2, QPointF{0.4, 0.3})});
    model.applyPerceptionFrame(occupied);
    QVERIFY(model.hasNearestPerson());

    model.applyPerceptionFrame(PerceptionFrame{});

    QVERIFY(!model.hasNearestPerson());
    QVERIFY(!model.isPersonPresent());
    QVERIFY(model.nearestHeadCenter().isNull());
    QCOMPARE(model.people()->rowCount(), 0);
}

void TestWorldModel::onlyNotifiesHeardTextOnAChange()
{
    WorldModel model;
    QSignalSpy spy{&model, &WorldModel::heardTextChanged};

    model.setHeardText(QStringLiteral("hello kerfur"));
    model.setHeardText(QStringLiteral("hello kerfur"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.heardText(), QStringLiteral("hello kerfur"));
}

void TestWorldModel::tracksCameraAndMicrophoneAvailability()
{
    WorldModel model;
    QVERIFY(!model.isCameraOnline());
    QVERIFY(!model.isMicrophoneOnline());

    model.setCameraOnline(true);
    model.setMicrophoneOnline(true);
    model.setCameraOrientation(180);

    QVERIFY(model.isCameraOnline());
    QVERIFY(model.isMicrophoneOnline());
    QCOMPARE(model.cameraOrientation(), 180);
}

void TestWorldModel::notifiesWhenAFrameChangesCameraOrientation()
{
    WorldModel model;
    QSignalSpy spy{&model, &WorldModel::cameraOrientationChanged};

    PerceptionFrame frame;
    frame.setOrientationDegrees(180);
    model.applyPerceptionFrame(frame);

    // The property declares NOTIFY cameraOrientationChanged, so a binding on it
    // alone has to update when a frame carries a new orientation.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.cameraOrientation(), 180);

    // Same orientation again is not a change and must not notify.
    model.applyPerceptionFrame(frame);
    QCOMPARE(spy.count(), 1);
}

QTEST_GUILESS_MAIN(TestWorldModel)
#include "tst_worldmodel.moc"
