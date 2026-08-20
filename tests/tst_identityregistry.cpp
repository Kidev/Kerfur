// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identityregistry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using namespace Kerfur;

namespace {

/// One file of the fixture. Void so that it can say which path it failed on:
/// a gallery built from a file that was never written fails later, somewhere
/// else, as an identity the registry simply did not find.
void writeFile(const QString &path, const QByteArray &contents)
{
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QFile file{path};
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    QCOMPARE(file.write(contents), static_cast<qint64>(contents.size()));
}

/// A gallery on disk: the manifest, plus empty files wherever it points, so
/// that the existence checks in the registry have something to find.
QString buildGallery(QTemporaryDir &root, const QString &manifest, const QStringList &files)
{
    const QDir directory{root.path()};
    for (const QString &relative : files) {
        writeFile(directory.absoluteFilePath(relative), QByteArrayLiteral("x"));
    }
    if (!manifest.isNull()) {
        writeFile(directory.absoluteFilePath(QStringLiteral("ids.json")), manifest.toUtf8());
    }
    return directory.absolutePath();
}

} // namespace

class TestIdentityRegistry : public QObject
{
    Q_OBJECT

private slots:
    void readsTheDeclaredLayout();
    void toleratesAHandEditedTrailingComma();
    void acceptsAPriorityWrittenAsAString();
    void resolvesPathsAgainstTheManifest();
    void keepsAnAbsolutePath();
    void fillsInConventionalPathsWhenNoneAreDeclared();
    void ranksAStrangerAfterEverybody();
    void ordersRecordsByRank();
    void acceptsAListInsteadOfAMap();
    void dropsAPathThatDoesNotExist();
    void fallsBackToScanningWhenThereIsNoManifest();
    void reportsAManifestThatIsNotJson();
    void splitsFaceAndVoiceEntries();
};

void TestIdentityRegistry::readsTheDeclaredLayout()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"kidev":{"name":"kidev","face":"faces/kidev.png",)"
                                    R"("voice":"voices/kidev.mp3","priority":0}})"),
                     {QStringLiteral("faces/kidev.png"), QStringLiteral("voices/kidev.mp3")})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QVERIFY(registry.hasManifest());
    QCOMPARE(registry.size(), 1);

    const std::optional<IdentityRecord> record{registry.find(QStringLiteral("kidev"))};
    QVERIFY(record.has_value());
    QCOMPARE(record->name, QStringLiteral("kidev"));
    QCOMPARE(record->priority, 0);
    QVERIFY(record->facePath.endsWith(QStringLiteral("faces/kidev.png")));
    QVERIFY(record->voicePath.endsWith(QStringLiteral("voices/kidev.mp3")));
}

void TestIdentityRegistry::toleratesAHandEditedTrailingComma()
{
    QTemporaryDir root;
    // Exactly the file a person writes at midnight. Strict JSON rejects it and
    // refusing to know anybody over a comma is the least useful reaction.
    const QString directory{
        buildGallery(root,
                     QStringLiteral("{\n \"kidev\": {\n  \"face\": \"faces/kidev.png\",\n"
                                    "  \"priority\": 0\n },\n}\n"),
                     {QStringLiteral("faces/kidev.png")})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QVERIFY(registry.hasManifest());
    QCOMPARE(registry.size(), 1);
    QCOMPARE(registry.priorityOf(QStringLiteral("kidev")), 0);
}

void TestIdentityRegistry::acceptsAPriorityWrittenAsAString()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"kidev":{"face":"faces/kidev.png","priority":"2"}})"),
                     {QStringLiteral("faces/kidev.png")})};

    QCOMPARE(IdentityRegistry::load(directory).priorityOf(QStringLiteral("kidev")), 2);
}

void TestIdentityRegistry::resolvesPathsAgainstTheManifest()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"ada":{"face":"portraits/ada.jpg"}})"),
                     {QStringLiteral("portraits/ada.jpg")})};

    const std::optional<IdentityRecord> record{
        IdentityRegistry::load(directory).find(QStringLiteral("ada"))};
    QVERIFY(record.has_value());
    QCOMPARE(record->facePath,
             QDir{directory}.absoluteFilePath(QStringLiteral("portraits/ada.jpg")));
}

void TestIdentityRegistry::keepsAnAbsolutePath()
{
    QTemporaryDir root;
    QTemporaryDir elsewhere;
    const QString shared{QDir{elsewhere.path()}.absoluteFilePath(QStringLiteral("ada.png"))};
    writeFile(shared, QByteArrayLiteral("x"));

    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"ada":{"face":"%1"}})").arg(shared),
                     {})};

    const std::optional<IdentityRecord> record{
        IdentityRegistry::load(directory).find(QStringLiteral("ada"))};
    QVERIFY(record.has_value());
    QCOMPARE(record->facePath, shared);
}

void TestIdentityRegistry::fillsInConventionalPathsWhenNoneAreDeclared()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"kidev":{"priority":1}})"),
                     {QStringLiteral("faces/kidev.png"), QStringLiteral("voices/kidev.mp3")})};

    const std::optional<IdentityRecord> record{
        IdentityRegistry::load(directory).find(QStringLiteral("kidev"))};
    QVERIFY(record.has_value());
    QVERIFY(record->facePath.endsWith(QStringLiteral("faces/kidev.png")));
    QVERIFY(record->voicePath.endsWith(QStringLiteral("voices/kidev.mp3")));
}

void TestIdentityRegistry::ranksAStrangerAfterEverybody()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root, QStringLiteral(R"({"kidev":{"priority":0}})"), {})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QCOMPARE(registry.priorityOf(QString{}), IdentityRegistry::unknownPriority);
    QCOMPARE(registry.priorityOf(QStringLiteral("nobody")), IdentityRegistry::unknownPriority);
    QVERIFY(IdentityRegistry::unknownPriority > 0);
}

void TestIdentityRegistry::ordersRecordsByRank()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"zoe":{"priority":2},"ada":{"priority":0},)"
                                    R"("bob":{"priority":1}})"),
                     {})};

    const QList<IdentityRecord> records{IdentityRegistry::load(directory).records()};
    QCOMPARE(records.size(), 3);
    QCOMPARE(records.at(0).id, QStringLiteral("ada"));
    QCOMPARE(records.at(1).id, QStringLiteral("bob"));
    QCOMPARE(records.at(2).id, QStringLiteral("zoe"));
}

void TestIdentityRegistry::acceptsAListInsteadOfAMap()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root,
                     QStringLiteral(R"({"identities":[{"id":"ada","priority":0},)"
                                    R"({"id":"bob","priority":1}]})"),
                     {})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QCOMPARE(registry.size(), 2);
    QCOMPARE(registry.priorityOf(QStringLiteral("bob")), 1);
}

void TestIdentityRegistry::dropsAPathThatDoesNotExist()
{
    QTemporaryDir root;
    const QString directory{
        buildGallery(root, QStringLiteral(R"({"ada":{"face":"faces/missing.png"}})"), {})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    const std::optional<IdentityRecord> record{registry.find(QStringLiteral("ada"))};
    QVERIFY(record.has_value());
    // Still a person, just one with nothing to enrol. Half a person is more
    // useful than none, and the problem says which half is missing.
    QVERIFY(record->facePath.isEmpty());
    QVERIFY(registry.problem().contains(QStringLiteral("does not exist")));
}

void TestIdentityRegistry::fallsBackToScanningWhenThereIsNoManifest()
{
    QTemporaryDir root;
    const QString directory{buildGallery(root,
                                         QString{},
                                         {QStringLiteral("faces/kidev.png"),
                                          QStringLiteral("voices/kidev.mp3"),
                                          QStringLiteral("ada.png")})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QVERIFY(!registry.hasManifest());
    QCOMPARE(registry.names(), (QStringList{QStringLiteral("ada"), QStringLiteral("kidev")}));
    // Nobody outranks anybody without a manifest to say so.
    QCOMPARE(registry.priorityOf(QStringLiteral("kidev")), IdentityRegistry::unknownPriority);
}

void TestIdentityRegistry::reportsAManifestThatIsNotJson()
{
    QTemporaryDir root;
    const QString directory{buildGallery(root,
                                         QStringLiteral("this is not json at all"),
                                         {QStringLiteral("faces/kidev.png")})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QVERIFY(!registry.problem().isEmpty());
    // And still finds the face, because the directory is right even when the
    // file in it is wrong.
    QCOMPARE(registry.names(), QStringList{QStringLiteral("kidev")});
}

void TestIdentityRegistry::splitsFaceAndVoiceEntries()
{
    QTemporaryDir root;
    const QString directory{buildGallery(root,
                                         QStringLiteral(R"({"kidev":{},"ada":{}})"),
                                         {QStringLiteral("faces/kidev.png"),
                                          QStringLiteral("voices/kidev.mp3"),
                                          QStringLiteral("faces/ada.png")})};

    const IdentityRegistry registry{IdentityRegistry::load(directory)};
    QCOMPARE(registry.faceEntries().size(), 2);
    QCOMPARE(registry.voiceEntries().size(), 1);
    QCOMPARE(registry.voiceEntries().first().first, QStringLiteral("kidev"));
}

QTEST_MAIN(TestIdentityRegistry)

#include "tst_identityregistry.moc"
