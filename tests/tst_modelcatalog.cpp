// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "modelcatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

using namespace Kerfur;

namespace {

constexpr ModelId allModels[]{ModelId::Pose,
                              ModelId::Objects,
                              ModelId::Face,
                              ModelId::Identity,
                              ModelId::Emotion,
                              ModelId::Palm,
                              ModelId::HandLandmarks,
                              ModelId::PoseHailo,
                              ModelId::ClipHailo,
                              ModelId::FaceHailo,
                              ModelId::IdentityHailo,
                              ModelId::HandLandmarksHailo};

QJsonObject readDocument()
{
    QFile file{QStringLiteral(KERFUR_SOURCE_DIR) + QStringLiteral("/assets/models.json")};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Returned by value and always copy initialized at the call sites. Braces would
// select QJsonArray's initializer list constructor, because QJsonValue converts
// implicitly from QJsonArray, and every entry would then be an empty object.
QJsonArray readManifest()
{
    return readDocument().value(QStringLiteral("models")).toArray();
}

} // namespace

/// The manifest provisions the files and the catalog looks them up, and the two
/// name them independently.
///
/// A model renamed in one and not the other produces a build that downloads
/// five files and then reports the stage as unavailable, with nothing anywhere
/// saying why. This test is the only thing binding them together.
class TestModelCatalog : public QObject
{
    Q_OBJECT

private slots:
    void manifestIsReadable();
    void everyStageHasAManifestEntry();
    void manifestFileNamesMatchTheCatalog();
    void everyEntryDeclaresAChecksumAndAProfile();
    void missingDirectoryDisablesEveryStage();
    void componentVersionCoversItsModels();
};

void TestModelCatalog::manifestIsReadable()
{
    QVERIFY(!readManifest().isEmpty());
}

void TestModelCatalog::everyStageHasAManifestEntry()
{
    QSet<QString> ids;
    const QJsonArray models = readManifest();
    for (const QJsonValue &value : models) {
        ids.insert(value.toObject().value(QStringLiteral("id")).toString());
    }

    for (const ModelId id : allModels) {
        QVERIFY2(ids.contains(ModelCatalog::stageName(id)),
                 qPrintable(QStringLiteral("no manifest entry for stage ")
                            + ModelCatalog::stageName(id)));
    }
}

void TestModelCatalog::manifestFileNamesMatchTheCatalog()
{
    const QJsonArray models = readManifest();
    for (const QJsonValue &value : models) {
        const QJsonObject entry{value.toObject()};
        const QString id{entry.value(QStringLiteral("id")).toString()};

        for (const ModelId candidate : allModels) {
            if (ModelCatalog::stageName(candidate) != id) {
                continue;
            }
            QCOMPARE(entry.value(QStringLiteral("file")).toString(),
                     ModelCatalog::fileName(candidate));
        }
    }
}

void TestModelCatalog::everyEntryDeclaresAChecksumAndAProfile()
{
    const QJsonArray models = readManifest();
    for (const QJsonValue &value : models) {
        const QJsonObject entry{value.toObject()};

        // The URL is a convenience and may be replaced; the checksum is the
        // authority, so an entry without one provisions nothing verifiable.
        QCOMPARE(entry.value(QStringLiteral("sha256")).toString().size(), 64);
        QVERIFY(!entry.value(QStringLiteral("url")).toString().isEmpty());
        QVERIFY(entry.value(QStringLiteral("bytes")).toInt() > 0);
        QVERIFY(!entry.value(QStringLiteral("profiles")).toArray().isEmpty());
    }
}

void TestModelCatalog::missingDirectoryDisablesEveryStage()
{
    // The state a fresh clone is in before `make models`. It has to degrade to
    // a face with no perception, not to a failed start.
    const ModelCatalog empty;
    QVERIFY(empty.directory().isEmpty());
    QCOMPARE(empty.missingStages(true).size(), std::size(allModels));
    qsizetype cpuStages{0};
    for (const ModelId id : allModels) {
        cpuStages += ModelCatalog::isAccelerated(id) ? 0 : 1;
    }
    QCOMPARE(empty.missingStages().size(), cpuStages);
    for (const ModelId id : allModels) {
        QVERIFY(!empty.has(id));
        QVERIFY(empty.path(id).isEmpty());
    }
}

void TestModelCatalog::componentVersionCoversItsModels()
{
    // The installer ships the `hailo` profile as a component whose version
    // decides whether an installed copy downloads it again. A model changed
    // under an unchanged version would never reach an installation.
    const QJsonObject hailo{readDocument()
                                .value(QStringLiteral("componentVersions"))
                                .toObject()
                                .value(QStringLiteral("hailo"))
                                .toObject()};
    QVERIFY(!hailo.value(QStringLiteral("version")).toString().isEmpty());

    QByteArray checksums;
    const QJsonArray models = readManifest();
    for (const QJsonValue &value : models) {
        const QJsonObject entry{value.toObject()};
        if (entry.value(QStringLiteral("profiles")).toArray().contains(QStringLiteral("hailo"))) {
            checksums += entry.value(QStringLiteral("sha256")).toString().toLatin1() + '\n';
        }
    }
    QVERIFY(!checksums.isEmpty());
    QCOMPARE(hailo.value(QStringLiteral("contents")).toString(),
             QString::fromLatin1(
                 QCryptographicHash::hash(checksums, QCryptographicHash::Sha256).toHex()));
}

QTEST_MAIN(TestModelCatalog)

#include "tst_modelcatalog.moc"
