// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "attention.h"

#include "identityregistry.h"
#include "perceptiontypes.h"

#include <QTest>

#include <optional>

using namespace Kerfur;

namespace {

AnalysedFace face(const QRectF &box,
                  int priority = IdentityRegistry::unknownPriority,
                  const QString &identity = {})
{
    AnalysedFace result;
    result.box = box;
    result.priority = priority;
    result.identity = identity;
    return result;
}

AnalysedPerson person(const QRectF &box, int priority = IdentityRegistry::unknownPriority)
{
    AnalysedPerson result;
    result.box = box;
    result.priority = priority;
    for (int index{0}; index < keypointCount; ++index) {
        result.keypoints.append(QPointF{});
        result.keypointScores.append(0.0);
    }
    return result;
}

} // namespace

class TestAttention : public QObject
{
    Q_OBJECT

private slots:
    void loneStrangerIsFollowed();
    void higherRankWins();
    void largerStrangerWinsWithoutHistory();
    void followedStrangerIsKept();
    void muchLargerStrangerTakesOver();
    void noBodyToFollow();
    void bodyByRankThenSize();
    void headFromKeypoints();
    void headFromBoxWithoutKeypoints();
    void smoothingFiltersJitter();
    void smoothingLetsMovesThrough();
    void smoothingOff();
};

void TestAttention::loneStrangerIsFollowed()
{
    QList<AnalysedFace> faces{face(QRectF{0.1, 0.1, 0.05, 0.05})};
    orderForAttention(faces, std::nullopt);
    QCOMPARE(faces.size(), 1);
    QVERIFY(faces.first().identity.isEmpty());
    QCOMPARE(faces.first().priority, IdentityRegistry::unknownPriority);
}

void TestAttention::higherRankWins()
{
    QList<AnalysedFace> faces{face(QRectF{0.0, 0.0, 0.4, 0.4}),
                              face(QRectF{0.6, 0.6, 0.05, 0.05}, 0, QStringLiteral("kidev"))};
    orderForAttention(faces, QPointF{0.2, 0.2});
    QCOMPARE(faces.first().identity, QStringLiteral("kidev"));
}

void TestAttention::largerStrangerWinsWithoutHistory()
{
    QList<AnalysedFace> faces{face(QRectF{0.0, 0.0, 0.1, 0.1}), face(QRectF{0.5, 0.5, 0.2, 0.2})};
    orderForAttention(faces, std::nullopt);
    QCOMPARE(faces.first().box, QRectF(0.5, 0.5, 0.2, 0.2));
}

void TestAttention::followedStrangerIsKept()
{
    // Slightly smaller than the other one, and already followed: kept.
    QList<AnalysedFace> faces{face(QRectF{0.5, 0.5, 0.2, 0.2}), face(QRectF{0.0, 0.0, 0.18, 0.18})};
    orderForAttention(faces, QPointF{0.1, 0.1});
    QCOMPARE(faces.first().box, QRectF(0.0, 0.0, 0.18, 0.18));
}

void TestAttention::muchLargerStrangerTakesOver()
{
    QList<AnalysedFace> faces{face(QRectF{0.5, 0.5, 0.4, 0.4}), face(QRectF{0.0, 0.0, 0.1, 0.1})};
    orderForAttention(faces, QPointF{0.05, 0.05});
    QCOMPARE(faces.first().box, QRectF(0.5, 0.5, 0.4, 0.4));
}

void TestAttention::noBodyToFollow()
{
    QCOMPARE(chooseBody({}, std::nullopt), qsizetype{-1});
}

void TestAttention::bodyByRankThenSize()
{
    const QList<AnalysedPerson> strangers{person(QRectF{0.0, 0.0, 0.2, 0.5}),
                                          person(QRectF{0.5, 0.0, 0.3, 0.8})};
    QCOMPARE(chooseBody(strangers, std::nullopt), qsizetype{1});

    const QList<AnalysedPerson> ranked{person(QRectF{0.0, 0.0, 0.2, 0.5}, 0),
                                       person(QRectF{0.5, 0.0, 0.3, 0.8})};
    QCOMPARE(chooseBody(ranked, std::nullopt), qsizetype{0});
}

void TestAttention::headFromKeypoints()
{
    AnalysedPerson body{person(QRectF{0.2, 0.2, 0.4, 0.6})};
    body.keypoints[static_cast<int>(Keypoint::LeftEye)] = QPointF{0.40, 0.30};
    body.keypointScores[static_cast<int>(Keypoint::LeftEye)] = 0.9;
    body.keypoints[static_cast<int>(Keypoint::RightEye)] = QPointF{0.30, 0.30};
    body.keypointScores[static_cast<int>(Keypoint::RightEye)] = 0.9;
    // Below the threshold, so left out of the mean.
    body.keypoints[static_cast<int>(Keypoint::Nose)] = QPointF{0.9, 0.9};
    body.keypointScores[static_cast<int>(Keypoint::Nose)] = 0.1;

    const QPointF head{headPoint(body, 0.4)};
    QVERIFY(qFuzzyCompare(head.x(), 0.35));
    QVERIFY(qFuzzyCompare(head.y(), 0.30));
}

void TestAttention::headFromBoxWithoutKeypoints()
{
    const QPointF head{headPoint(person(QRectF{0.2, 0.2, 0.4, 0.6}), 0.4)};
    QVERIFY(qFuzzyCompare(head.x(), 0.4));
    QVERIFY(qFuzzyCompare(head.y(), 0.26));
}

void TestAttention::smoothingFiltersJitter()
{
    QVERIFY(smoothingWeight(0.6, 0.0, 4.0) == 0.6);
    QVERIFY(smoothingWeight(0.6, 0.5, 4.0) > 0.5);
}

void TestAttention::smoothingLetsMovesThrough()
{
    QVERIFY(smoothingWeight(0.9, 20.0, 4.0) < 0.01);
    QVERIFY(smoothingWeight(0.9, -20.0, 4.0) < 0.01);
    QVERIFY(smoothingWeight(0.9, 8.0, 4.0) < smoothingWeight(0.9, 4.0, 4.0));
}

void TestAttention::smoothingOff()
{
    QCOMPARE(smoothingWeight(0.0, 0.0, 4.0), 0.0);
    QCOMPARE(smoothingWeight(0.5, 0.0, 0.0), 0.0);
}

QTEST_GUILESS_MAIN(TestAttention)

#include "tst_attention.moc"
