// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "facepipeline.h"
#include "identitystore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <opencv2/core.hpp>

using namespace Kerfur;

namespace {

// SFace's embedding width. The exact number does not matter to the store, but
// using the real one keeps the round trip honest about how much it writes.
constexpr int embeddingWidth{128};

cv::Mat makeEmbedding(float seed)
{
    cv::Mat embedding{cv::Mat::zeros(1, embeddingWidth, CV_32F)};
    for (int column{0}; column < embeddingWidth; ++column) {
        embedding.at<float>(0, column) = seed + (static_cast<float>(column) * 0.01F);
    }
    return embedding;
}

} // namespace

/// The gallery and its on disk cache.
///
/// The cache is what makes a restart cheap, and a cache that reads back
/// slightly wrong is the worst possible outcome: every known person silently
/// becomes a stranger, with a populated file on disk saying otherwise.
class TestIdentityStore : public QObject
{
    Q_OBJECT

private slots:
    void cosineSimilarityHandlesDegenerateInput();
    void matchesTheNearestEntry();
    void reportsNearMissesBelowTheThreshold();
    void survivesACacheRoundTrip();
    void enrolWithoutARecogniserLeavesTheCacheAlone();
};

void TestIdentityStore::cosineSimilarityHandlesDegenerateInput()
{
    const cv::Mat embedding{makeEmbedding(0.5F)};
    QVERIFY(qAbs(IdentityStore::cosineSimilarity(embedding, embedding) - 1.0) < 1e-6);

    QCOMPARE(IdentityStore::cosineSimilarity(embedding, cv::Mat{}), 0.0);
    QCOMPARE(IdentityStore::cosineSimilarity(cv::Mat{}, cv::Mat{}), 0.0);

    // A different width is a different model, not a distant person, and
    // comparing them anyway would read past the shorter buffer.
    const cv::Mat shorter{cv::Mat::zeros(1, 64, CV_32F)};
    QCOMPARE(IdentityStore::cosineSimilarity(embedding, shorter), 0.0);

    const cv::Mat zeroes{cv::Mat::zeros(1, embeddingWidth, CV_32F)};
    QCOMPARE(IdentityStore::cosineSimilarity(embedding, zeroes), 0.0);
}

void TestIdentityStore::matchesTheNearestEntry()
{
    IdentityStore store;
    store.insert(QStringLiteral("kidev"), QStringLiteral("aaa"), makeEmbedding(1.0F));
    store.insert(QStringLiteral("someone"), QStringLiteral("bbb"), makeEmbedding(-1.0F));
    QCOMPARE(store.size(), 2);

    double similarity{0.0};
    QCOMPARE(store.match(makeEmbedding(1.0F), similarity), QStringLiteral("kidev"));
    QVERIFY(similarity > 0.99);
}

void TestIdentityStore::reportsNearMissesBelowTheThreshold()
{
    IdentityStore store;
    store.insert(QStringLiteral("kidev"), QStringLiteral("aaa"), makeEmbedding(1.0F));
    store.setThreshold(1.5);

    // The similarity is written whether or not it clears the bar, so a caller
    // can log how near a miss was rather than only that there was one.
    double similarity{0.0};
    QVERIFY(store.match(makeEmbedding(1.0F), similarity).isEmpty());
    QVERIFY(similarity > 0.99);
}

void TestIdentityStore::survivesACacheRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString cachePath{directory.filePath(QStringLiteral("identities.json"))};

    IdentityStore written;
    written.insert(QStringLiteral("kidev"), QStringLiteral("hash-one"), makeEmbedding(1.0F));
    written.insert(QStringLiteral("someone"), QStringLiteral("hash-two"), makeEmbedding(-1.0F));
    QVERIFY(written.saveTo(cachePath));
    QVERIFY(QFile::exists(cachePath));

    IdentityStore read;
    QVERIFY(read.loadFrom(cachePath));
    QCOMPARE(read.size(), 2);

    // The embedding has to come back as a 1 by N float row. OpenCV's Mat has an
    // initializer_list constructor, so building it with braces from three ints
    // yields a three element column vector of those integers instead, and every
    // value read out of it afterwards is a reinterpreted integer that still
    // looks like a plausible number.
    double similarity{0.0};
    QCOMPARE(read.match(makeEmbedding(1.0F), similarity), QStringLiteral("kidev"));
    QVERIFY(similarity > 0.99);
}

void TestIdentityStore::enrolWithoutARecogniserLeavesTheCacheAlone()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString cachePath{directory.filePath(QStringLiteral("identities.json"))};
    const QString galleryPath{directory.filePath(QStringLiteral("faces"))};
    QVERIFY(QDir{}.mkpath(galleryPath));

    IdentityStore written;
    written.insert(QStringLiteral("kidev"), QStringLiteral("hash-one"), makeEmbedding(1.0F));
    QVERIFY(written.saveTo(cachePath));
    const qint64 size{QFileInfo{cachePath}.size()};

    // A build without the identity model must not rewrite the enrolment of one
    // that has it, which is what an unguarded enrolment pass would do: every
    // image fails to embed, the refreshed list comes out empty, and the cache
    // is overwritten with nothing.
    FacePipeline pipeline;
    QVERIFY(!pipeline.hasRecognizer());

    IdentityStore store;
    store.enrol(pipeline, galleryPath, cachePath);
    QCOMPARE(QFileInfo{cachePath}.size(), size);

    IdentityStore read;
    QVERIFY(read.loadFrom(cachePath));
    QCOMPARE(read.size(), 1);
}

QTEST_MAIN(TestIdentityStore)

#include "tst_identitystore.moc"
