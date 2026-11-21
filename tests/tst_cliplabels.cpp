// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "clipclassifier.h"
#include "modelcatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

#include <cmath>

using namespace Kerfur;

namespace {

QByteArray readSource(const QString &relative)
{
    QFile file{QStringLiteral(KERFUR_SOURCE_DIR "/") + relative};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

/// An LVIS name as the generator labels it: underscores spoken and a final
/// disambiguation in parentheses dropped.
QString lvisLabel(QString name)
{
    static const QRegularExpression disambiguation{QStringLiteral("_\\([^)]*\\)$")};
    return name.remove(disambiguation).replace(QLatin1Char('_'), QLatin1Char(' '));
}

const QString shippedLabels{QStringLiteral("assets/clip/clip_vit_l_14_laion2B_image_encoder.json")};

/// Three unit rows along the first three axes of a four value space.
ClipSet axes(const QStringList &labels)
{
    ClipSet set;
    set.labels = labels;
    set.embeddings = cv::Mat::zeros(3, 4, CV_32F);
    for (int row{0}; row < 3; ++row) {
        set.embeddings.at<float>(row, row) = 1.0F;
    }
    return set;
}

} // namespace

/// The text side of CLIP is generated once and shipped; these check that what
/// ships still answers the vocabulary and the encoder it claims to.
class TestClipLabels : public QObject
{
    Q_OBJECT

private slots:
    void shippedLabelsFollowTheVocabulary();
    void shippedLabelsFitTheirEncoder();
    void shippedEmbeddingsAreUnitLength();
    void zeroShotPicksTheNearestLabel();
    void zeroShotCanAnswerNothing();
    void zeroShotAddsUpSynonyms();
    void parseReadsHalfPrecision();
    void parseRefusesShortEmbeddings();
    void parseRefusesAnythingElse();
    void squareCropPadsOutsideTheFrame();
};

void TestClipLabels::shippedLabelsFollowTheVocabulary()
{
    const QByteArray vocabularyBytes{readSource(QStringLiteral("assets/clip/vocabulary.json"))};
    const QJsonObject vocabulary{QJsonDocument::fromJson(vocabularyBytes).object()};
    const QJsonObject shipped{QJsonDocument::fromJson(readSource(shippedLabels)).object()};
    QVERIFY(!vocabulary.isEmpty());

    // The digest covers the vocabulary and the extra class files it names.
    // A mismatch means one was edited after the embeddings were made: run
    // cmake/clip-labels.py.
    QCryptographicHash digest{QCryptographicHash::Sha256};
    digest.addData(vocabularyBytes);
    for (auto entry{vocabulary.constBegin()}; entry != vocabulary.constEnd(); ++entry) {
        const QString extra{
            entry.value().toObject().value(QStringLiteral("extraClasses")).toString()};
        if (!entry.key().startsWith(QLatin1Char('$')) && !extra.isEmpty()) {
            digest.addData(readSource(QStringLiteral("assets/clip/") + extra));
        }
    }
    QCOMPARE(shipped.value(QStringLiteral("vocabularySha256")).toString(),
             QString::fromLatin1(digest.result().toHex()));

    const QJsonObject sets{shipped.value(QStringLiteral("sets")).toObject()};
    for (auto entry{vocabulary.constBegin()}; entry != vocabulary.constEnd(); ++entry) {
        if (entry.key().startsWith(QLatin1Char('$'))) {
            continue;
        }
        const QJsonObject set{entry.value().toObject()};

        // The set's own classes, then the extra ones it does not already
        // name, then the answer for nothing.
        QStringList own;
        for (const QJsonValue &item : set.value(QStringLiteral("classes")).toArray()) {
            own.append(item.toObject().value(QStringLiteral("label")).toString());
        }
        QSet<QString> taken;
        for (const QString &label : std::as_const(own)) {
            taken.insert(label.toLower());
        }
        QSet<QString> allowed{own.cbegin(), own.cend()};
        qsizetype rows{own.size()};
        const QString extra{set.value(QStringLiteral("extraClasses")).toString()};
        const QJsonObject exclude{set.value(QStringLiteral("exclude")).toObject()};
        QSet<QString> excludedKinds;
        for (const QJsonValue &kind : exclude.value(QStringLiteral("kindsOf")).toArray()) {
            excludedKinds.insert(kind.toString());
        }
        QSet<QString> excludedNames;
        for (const QJsonValue &name : exclude.value(QStringLiteral("names")).toArray()) {
            excludedNames.insert(name.toString());
        }
        if (!extra.isEmpty()) {
            const QJsonArray categories =
                QJsonDocument::fromJson(readSource(QStringLiteral("assets/clip/") + extra))
                    .object()
                    .value(QStringLiteral("categories"))
                    .toArray();
            for (const QJsonValue &value : categories) {
                const QJsonObject category{value.toObject()};
                const QString name{category.value(QStringLiteral("name")).toString()};
                bool ruledOut{excludedNames.contains(name)};
                for (const QJsonValue &kind : category.value(QStringLiteral("kinds")).toArray()) {
                    ruledOut = ruledOut || excludedKinds.contains(kind.toString());
                }
                if (ruledOut) {
                    continue;
                }
                const QString label{lvisLabel(name)};
                bool named{taken.contains(label.toLower())};
                const QJsonArray synonyms = category.value(QStringLiteral("synonyms")).toArray();
                for (const QJsonValue &synonym : synonyms) {
                    named = named || taken.contains(lvisLabel(synonym.toString()).toLower());
                }
                if (!named) {
                    allowed.insert(label);
                    ++rows;
                }
            }
        }
        const bool none{!set.value(QStringLiteral("none")).toArray().isEmpty()};
        rows += none ? 1 : 0;

        QStringList labels;
        for (const QJsonValue &label :
             sets.value(entry.key()).toObject().value(QStringLiteral("labels")).toArray()) {
            labels.append(label.toString());
        }
        QCOMPARE(labels.size(), rows);
        QCOMPARE(labels.first(), own.first());
        QCOMPARE(labels.last().isEmpty(), none);
        for (qsizetype index{0}; index < labels.size() - (none ? 1 : 0); ++index) {
            QVERIFY2(allowed.contains(labels.at(index)), qPrintable(labels.at(index)));
        }
    }
}

void TestClipLabels::shippedLabelsFitTheirEncoder()
{
    const QJsonObject shipped{QJsonDocument::fromJson(readSource(shippedLabels)).object()};
    const QString encoder{shipped.value(QStringLiteral("encoder")).toString()};
    QCOMPARE(encoder, ModelCatalog::fileName(ModelId::ClipHailo));

    // Copy initialized: braces would make an array holding this array.
    const QJsonArray models = QJsonDocument::fromJson(
                                  readSource(QStringLiteral("assets/models.json")))
                                  .object()
                                  .value(QStringLiteral("models"))
                                  .toArray();
    QString pinned;
    for (const QJsonValue &model : models) {
        if (model.toObject().value(QStringLiteral("file")).toString() == encoder) {
            pinned = model.toObject().value(QStringLiteral("sha256")).toString();
        }
    }
    QVERIFY(!pinned.isEmpty());
    QCOMPARE(shipped.value(QStringLiteral("encoderSha256")).toString(), pinned);
}

void TestClipLabels::shippedEmbeddingsAreUnitLength()
{
    QString problem;
    const std::optional<ClipLabels> labels{parseClipLabels(readSource(shippedLabels), problem)};
    QVERIFY2(labels.has_value(), qPrintable(problem));
    QVERIFY(labels->sets.contains(QStringLiteral("held")));
    QVERIFY(labels->sets.contains(QStringLiteral("expression")));
    for (const ClipSet &set : labels->sets) {
        for (int row{0}; row < set.embeddings.rows; ++row) {
            QVERIFY(std::abs(cv::norm(set.embeddings.row(row)) - 1.0) < 1e-4);
        }
    }
}

void TestClipLabels::zeroShotPicksTheNearestLabel()
{
    const ClipSet set{
        axes({QStringLiteral("cup"), QStringLiteral("phone"), QStringLiteral("pen")})};
    const cv::Mat image{(cv::Mat_<float>(1, 4) << 0.1F, 0.9F, 0.0F, 0.2F)};
    const ClipAnswer answer{zeroShot(image, set, 100.0F)};
    QCOMPARE(answer.label, QStringLiteral("phone"));
    QVERIFY(answer.probability > 0.99F);
}

void TestClipLabels::zeroShotCanAnswerNothing()
{
    const ClipSet set{axes({QStringLiteral("cup"), QStringLiteral("phone"), QString{}})};
    const cv::Mat image{(cv::Mat_<float>(1, 4) << 0.2F, 0.1F, 0.9F, 0.0F)};
    QVERIFY(zeroShot(image, set, 100.0F).label.isEmpty());
}

void TestClipLabels::zeroShotAddsUpSynonyms()
{
    // Alone, the phone row is the closest (0.41 against 0.29 each), but the
    // two mug rows are one answer and hold 0.59 between them.
    const ClipSet set{
        axes({QStringLiteral("mug"), QStringLiteral("mug"), QStringLiteral("phone")})};
    const cv::Mat image{(cv::Mat_<float>(1, 4) << 0.66F, 0.66F, 0.7F, 0.0F)};
    const ClipAnswer answer{zeroShot(image, set, 10.0F)};
    QCOMPARE(answer.label, QStringLiteral("mug"));
    QVERIFY(std::abs(answer.probability - 0.587F) < 0.005F);
}

void TestClipLabels::parseReadsHalfPrecision()
{
    // One label of four halves: 1.0, -2.0, 0.5, 0.0, little endian.
    const QByteArray halves{QByteArray::fromHex("003c00c000380000")};
    const QByteArray json{"{\"encoder\": \"e.hef\", \"dimension\": 4, \"dtype\": \"float16\", "
                          "\"sets\": {\"held\": {\"labels\": [\"a\"], \"embeddings\": \""
                          + halves.toBase64() + "\"}}}"};
    QString problem;
    const std::optional<ClipLabels> labels{parseClipLabels(json, problem)};
    QVERIFY2(labels.has_value(), qPrintable(problem));
    const cv::Mat &row{labels->sets.value(QStringLiteral("held")).embeddings};
    QCOMPARE(row.type(), CV_32F);
    QCOMPARE(row.at<float>(0, 0), 1.0F);
    QCOMPARE(row.at<float>(0, 1), -2.0F);
    QCOMPARE(row.at<float>(0, 2), 0.5F);
}

void TestClipLabels::parseRefusesShortEmbeddings()
{
    // Two labels of four values need 32 bytes; this holds 16.
    const QByteArray packed{QByteArray(16, '\0').toBase64()};
    const QByteArray json{"{\"encoder\": \"e.hef\", \"dimension\": 4, \"sets\": {\"held\": "
                          "{\"labels\": [\"a\", \"b\"], \"embeddings\": \"" +
                          packed + "\"}}}"};
    QString problem;
    QVERIFY(!parseClipLabels(json, problem).has_value());
    QVERIFY(problem.contains(QStringLiteral("held")));

    const QByteArray whole{QByteArray(32, '\0').toBase64()};
    const QByteArray good{"{\"encoder\": \"e.hef\", \"dimension\": 4, \"sets\": {\"held\": "
                          "{\"labels\": [\"a\", \"b\"], \"embeddings\": \"" +
                          whole + "\"}}}"};
    QVERIFY(parseClipLabels(good, problem).has_value());
}

void TestClipLabels::parseRefusesAnythingElse()
{
    QString problem;
    QVERIFY(!parseClipLabels(QByteArrayLiteral("not json"), problem).has_value());
    QVERIFY(!parseClipLabels(QByteArrayLiteral("{\"dimension\": 4}"), problem).has_value());
    QVERIFY(
        !parseClipLabels(QByteArrayLiteral("{\"encoder\": \"e.hef\", \"dimension\": 4}"), problem)
             .has_value());
}

void TestClipLabels::squareCropPadsOutsideTheFrame()
{
    const cv::Mat frame{cv::Mat(100, 100, CV_8UC3, cv::Scalar{255, 255, 255})};
    // 40 wide and 20 tall, half of it left of the frame.
    const cv::Mat crop{squareCrop(frame, cv::Rect{-20, 40, 40, 20})};
    QCOMPARE(crop.cols, 40);
    QCOMPARE(crop.rows, 40);
    QCOMPARE(crop.at<cv::Vec3b>(20, 5), (cv::Vec3b{0, 0, 0}));
    QCOMPARE(crop.at<cv::Vec3b>(20, 35), (cv::Vec3b{255, 255, 255}));
}

QTEST_GUILESS_MAIN(TestClipLabels)

#include "tst_cliplabels.moc"
