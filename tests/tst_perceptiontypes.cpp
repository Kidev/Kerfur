// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "perceptiontypes.h"

#include <QTest>

using Kerfur::Keypoint;
using Kerfur::Keypoint2D;
using Kerfur::PerceptionFrame;
using Kerfur::TrackedPerson;

class TestPerceptionTypes : public QObject
{
    Q_OBJECT

private slots:
    void headCentreAveragesConfidentFacialKeypoints();
    void headCentreIgnoresUnconfidentKeypoints();
    void headCentreFallsBackToTheBoxTopWhenNothingIsConfident();
    void trackedPersonRoundTripsItsFields();
    void perceptionFrameCarriesItsPeople();
};

static QList<Keypoint2D> makeKeypoints(float confidence)
{
    QList<Keypoint2D> keypoints;
    keypoints.reserve(Kerfur::keypointCount);
    for (int index{0}; index < Kerfur::keypointCount; ++index) {
        keypoints.append(Keypoint2D{QPointF{0.9, 0.9}, 0.0F});
    }
    keypoints[static_cast<int>(Keypoint::Nose)] = Keypoint2D{QPointF{0.50, 0.20}, confidence};
    keypoints[static_cast<int>(Keypoint::LeftEye)] = Keypoint2D{QPointF{0.46, 0.18}, confidence};
    keypoints[static_cast<int>(Keypoint::RightEye)] = Keypoint2D{QPointF{0.54, 0.18}, confidence};
    return keypoints;
}

void TestPerceptionTypes::headCentreAveragesConfidentFacialKeypoints()
{
    const QPointF centre{Kerfur::headCenterFromKeypoints(makeKeypoints(0.9F), 0.5F)};
    QVERIFY(qAbs(centre.x() - 0.50) < 0.0001);
    QVERIFY(qAbs(centre.y() - (0.20 + 0.18 + 0.18) / 3.0) < 0.0001);
}

void TestPerceptionTypes::headCentreIgnoresUnconfidentKeypoints()
{
    QList<Keypoint2D> keypoints{makeKeypoints(0.9F)};

    // Drag one eye far off and mark it unbelievable. A confidence filter that
    // does not work would pull the centre with it.
    keypoints[static_cast<int>(Keypoint::LeftEye)] = Keypoint2D{QPointF{0.05, 0.95}, 0.1F};

    const QPointF centre{Kerfur::headCenterFromKeypoints(keypoints, 0.5F)};
    QVERIFY(qAbs(centre.x() - (0.50 + 0.54) / 2.0) < 0.0001);
}

void TestPerceptionTypes::headCentreFallsBackToTheBoxTopWhenNothingIsConfident()
{
    // Nothing believable at all is a real case: a person facing away has no
    // detectable face. Returning a null point lets the caller decide, rather
    // than returning an average of noise that looks like a real answer.
    const QPointF centre{Kerfur::headCenterFromKeypoints(makeKeypoints(0.1F), 0.5F)};
    QVERIFY(centre.isNull());
}

void TestPerceptionTypes::trackedPersonRoundTripsItsFields()
{
    TrackedPerson person;
    person.setTrackId(7);
    person.setBoundingBox(QRectF{0.1, 0.2, 0.3, 0.4});
    person.setHeadCenter(QPointF{0.25, 0.25});
    person.setProximity(0.4);
    person.setIdentity(QStringLiteral("kidev"), 0.82);
    person.setEmotion(QStringLiteral("happy"), 0.61);
    person.setHeldObjects({QStringLiteral("cup")});
    person.setKeypoints(makeKeypoints(0.9F));

    QCOMPARE(person.trackId(), 7);
    QCOMPARE(person.boundingBox(), QRectF(0.1, 0.2, 0.3, 0.4));
    QCOMPARE(person.headCenter(), QPointF(0.25, 0.25));
    QCOMPARE(person.proximity(), 0.4);
    QCOMPARE(person.identity(), QStringLiteral("kidev"));
    QCOMPARE(person.identityConfidence(), 0.82);
    QCOMPARE(person.emotion(), QStringLiteral("happy"));
    QCOMPARE(person.emotionConfidence(), 0.61);
    QCOMPARE(person.heldObjects(), QStringList{QStringLiteral("cup")});
    QCOMPARE(person.keypoints().size(), Kerfur::keypointCount);
}

void TestPerceptionTypes::perceptionFrameCarriesItsPeople()
{
    TrackedPerson person;
    person.setTrackId(3);

    PerceptionFrame frame;
    frame.setTimestampMs(1234);
    frame.setSequence(9);
    frame.setOrientationDegrees(180);
    frame.setPeople({person});

    QCOMPARE(frame.timestampMs(), 1234);
    QCOMPARE(frame.sequence(), 9U);
    QCOMPARE(frame.orientationDegrees(), 180);
    QCOMPARE(frame.people().size(), 1);
    QCOMPARE(frame.people().first().trackId(), 3);
}

QTEST_APPLESS_MAIN(TestPerceptionTypes)
#include "tst_perceptiontypes.moc"
