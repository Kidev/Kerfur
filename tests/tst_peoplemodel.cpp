// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "peoplemodel.h"
#include "perceptiontypes.h"

#include <QSignalSpy>
#include <QTest>

using Kerfur::PeopleModel;
using Kerfur::TrackedPerson;

static TrackedPerson makePerson(int trackId, const QString &emotion)
{
    TrackedPerson person;
    person.setTrackId(trackId);
    person.setProximity(0.1 * trackId);
    person.setEmotion(emotion, 0.9);
    return person;
}

class TestPeopleModel : public QObject
{
    Q_OBJECT

private slots:
    void startsEmpty();
    void exposesTheExpectedRoleNames();
    void insertsNewTracks();
    void updatesAnExistingTrackWithoutReinserting();
    void removesAVanishedTrack();
    void keepsRowsOrderedByTrackId();
};

void TestPeopleModel::startsEmpty()
{
    PeopleModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
}

void TestPeopleModel::exposesTheExpectedRoleNames()
{
    PeopleModel model;
    const QHash<int, QByteArray> names{model.roleNames()};
    QCOMPARE(names.value(PeopleModel::TrackIdRole), QByteArray{"trackId"});
    QCOMPARE(names.value(PeopleModel::IdentityRole), QByteArray{"identity"});
    QCOMPARE(names.value(PeopleModel::EmotionRole), QByteArray{"emotion"});
    QCOMPARE(names.value(PeopleModel::HeldObjectsRole), QByteArray{"heldObjects"});
}

void TestPeopleModel::insertsNewTracks()
{
    PeopleModel model;
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};

    model.setPeople(
        {makePerson(1, QStringLiteral("neutral")), makePerson(2, QStringLiteral("happy"))});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.data(model.index(0), PeopleModel::TrackIdRole).toInt(), 1);
    QCOMPARE(model.data(model.index(1), PeopleModel::EmotionRole).toString(),
             QStringLiteral("happy"));
}

void TestPeopleModel::updatesAnExistingTrackWithoutReinserting()
{
    PeopleModel model;
    model.setPeople({makePerson(1, QStringLiteral("neutral"))});

    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};

    model.setPeople({makePerson(1, QStringLiteral("angry"))});

    // The whole point of diffing: a delegate for track 1 survives the update.
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.data(model.index(0), PeopleModel::EmotionRole).toString(),
             QStringLiteral("angry"));
}

void TestPeopleModel::removesAVanishedTrack()
{
    PeopleModel model;
    model.setPeople({makePerson(1, QStringLiteral("neutral")),
                     makePerson(2, QStringLiteral("happy")),
                     makePerson(3, QStringLiteral("sad"))});

    QSignalSpy removed{&model, &QAbstractItemModel::rowsRemoved};
    model.setPeople(
        {makePerson(1, QStringLiteral("neutral")), makePerson(3, QStringLiteral("sad"))});

    QCOMPARE(removed.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1), PeopleModel::TrackIdRole).toInt(), 3);
}

void TestPeopleModel::keepsRowsOrderedByTrackId()
{
    PeopleModel model;
    model.setPeople({makePerson(9, QStringLiteral("a")),
                     makePerson(2, QStringLiteral("b")),
                     makePerson(5, QStringLiteral("c"))});

    QCOMPARE(model.data(model.index(0), PeopleModel::TrackIdRole).toInt(), 2);
    QCOMPARE(model.data(model.index(1), PeopleModel::TrackIdRole).toInt(), 5);
    QCOMPARE(model.data(model.index(2), PeopleModel::TrackIdRole).toInt(), 9);
}

QTEST_GUILESS_MAIN(TestPeopleModel)
#include "tst_peoplemodel.moc"
