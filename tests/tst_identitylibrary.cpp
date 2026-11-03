// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitydirectory.h"
#include "identitylibrary.h"
#include "identityregistry.h"
#include "wavfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>
#include <optional>

using namespace Kerfur;

namespace {

QImage picture()
{
    QImage image{64, 48, QImage::Format_RGB32};
    image.fill(Qt::darkCyan);
    return image;
}

std::vector<float> sentence(double seconds, int rate = 16000)
{
    std::vector<float> samples;
    const auto count{static_cast<size_t>(seconds * rate)};
    samples.reserve(count);
    for (size_t index{0}; index < count; ++index) {
        samples.push_back(static_cast<float>(
            0.3 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(index) / rate)));
    }
    return samples;
}

QJsonObject manifestIn(const QString &directory)
{
    QFile file{QDir{directory}.absoluteFilePath(IdentityRegistry::manifestFileName())};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QFile file{path};
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    QCOMPARE(file.write(contents), static_cast<qint64>(contents.size()));
}

} // namespace

class TestIdentityLibrary : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Test mode moves the application data location under ~/.qttest, so
        // the writable gallery of a real installation is never touched.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("kidev"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_identitylibrary"));
    }

    void cleanupTestCase()
    {
        QDir{IdentityDirectory::writableDirectory()}.removeRecursively();
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void init()
    {
        QDir{IdentityDirectory::writableDirectory()}.removeRecursively();
        QSettings settings;
        settings.remove(QStringLiteral("Paths/identitiesPath"));
        settings.sync();
    }

    void anIdIsLowerCaseAsciiWithSingleDashes()
    {
        QCOMPARE(IdentityLibrary::idFor(QStringLiteral("Anne-Marie  Dupr\u00e9")),
                 QStringLiteral("anne-marie-dupre"));
        QCOMPARE(IdentityLibrary::idFor(QStringLiteral("  kidev ")), QStringLiteral("kidev"));
        QCOMPARE(IdentityLibrary::idFor(QStringLiteral("R2_D2.")), QStringLiteral("r2-d2"));
    }

    void anIdCannotNameAnotherDirectory()
    {
        const QString id{IdentityLibrary::idFor(QStringLiteral("../../etc/passwd"))};
        QCOMPARE(id, QStringLiteral("etcpasswd"));
        QCOMPARE(IdentityLibrary::idFor(QStringLiteral("C:\\Windows")), QStringLiteral("cwindows"));
    }

    void anIdIsBounded()
    {
        const QString id{IdentityLibrary::idFor(QString(200, QLatin1Char('a')))};
        QCOMPARE(id.size(), IdentityLibrary::maximumIdLength);
        // A dash is never left hanging at the cut.
        const QString words{IdentityLibrary::idFor(QString(47, QLatin1Char('b')) + QStringLiteral(" c"))};
        QCOMPARE(words, QString(47, QLatin1Char('b')));
    }

    void aNameWithNothingUsableIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("test")));

        QVERIFY(!library.enrol(QStringLiteral(" ... "), QString{}, 2));
        QVERIFY(!library.problem().isEmpty());
        QVERIFY(!QFileInfo::exists(directory.filePath(IdentityRegistry::manifestFileName())));
    }

    void enrolWithNothingStagedIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QSignalSpy changed{&library, &IdentityLibrary::galleryChanged};

        QVERIFY(!library.enrol(QStringLiteral("Ada"), QString{}, 2));
        QVERIFY(!library.problem().isEmpty());
        QCOMPARE(changed.count(), 0);
        QVERIFY(!QFileInfo::exists(directory.filePath(IdentityRegistry::manifestFileName())));
    }

    void enrolWritesAStagedFace()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QVERIFY(library.isReady());
        QSignalSpy changed{&library, &IdentityLibrary::galleryChanged};

        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QCOMPARE(library.stagedFace(), QStringLiteral("camera, 64x48"));
        QVERIFY(library.enrol(QStringLiteral("Ada Lovelace"), QStringLiteral("trusted"), 2));

        QCOMPARE(changed.count(), 1);
        QVERIFY(library.problem().isEmpty());
        QVERIFY(library.stagedFace().isEmpty());

        const IdentityRegistry registry{IdentityRegistry::load(directory.path())};
        const std::optional<IdentityRecord> ada{registry.find(QStringLiteral("ada-lovelace"))};
        QVERIFY(ada.has_value());
        QCOMPARE(ada->name, QStringLiteral("Ada Lovelace"));
        QCOMPARE(ada->title, QStringLiteral("trusted"));
        QCOMPARE(ada->priority, 2);
        QCOMPARE(ada->facePath, directory.filePath(QStringLiteral("faces/ada-lovelace.png")));
        QVERIFY(ada->voicePath.isEmpty());
        QVERIFY(!QImage{ada->facePath}.isNull());

        QCOMPARE(library.people().size(), 1);
        const QVariantMap person{library.people().first().toMap()};
        QCOMPARE(person.value(QStringLiteral("id")).toString(), QStringLiteral("ada-lovelace"));
        QCOMPARE(person.value(QStringLiteral("standing")).toString(), QStringLiteral("trusted"));
        QVERIFY(person.value(QStringLiteral("hasFace")).toBool());
        QVERIFY(!person.value(QStringLiteral("hasVoice")).toBool());
    }

    void aLargePictureIsReduced()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QImage large{4000, 3000, QImage::Format_RGB32};
        large.fill(Qt::gray);

        QVERIFY(library.stageFaceImage(large, QStringLiteral("photo")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, 2));
        const QImage stored{directory.filePath(QStringLiteral("faces/ada.png"))};
        QCOMPARE(stored.width(), IdentityLibrary::maximumFaceSide);
        QCOMPARE(stored.height(), 1440);
    }

    void aKnownPersonIsUpdatedWithoutStaging()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QStringLiteral("trusted"), 2));

        QVERIFY(library.enrol(QStringLiteral("ada"), QStringLiteral("owner"), 0));

        const std::optional<IdentityRecord> ada{
            IdentityRegistry::load(directory.path()).find(QStringLiteral("ada"))};
        QVERIFY(ada.has_value());
        QCOMPARE(ada->title, QStringLiteral("owner"));
        QCOMPARE(ada->priority, 0);
        QCOMPARE(ada->facePath, directory.filePath(QStringLiteral("faces/ada.png")));
    }

    void aRankIsClampedIntoTheDeclarableRange()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, -3));
        QCOMPARE(IdentityRegistry::load(directory.path()).priorityOf(QStringLiteral("ada")), 0);

        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, IdentityRegistry::unknownPriority));
        QCOMPARE(IdentityRegistry::load(directory.path()).priorityOf(QStringLiteral("ada")),
                 IdentityLibrary::maximumPriority);
    }

    void aSentenceIsStoredAsAWave()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        const std::vector<float> spoken{sentence(2.5)};

        QVERIFY(library.stageVoiceSamples(spoken, 16000, QStringLiteral("last sentence"), 2.0));
        QCOMPARE(library.stagedVoice(), QStringLiteral("last sentence, 2.5 s"));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, 2));

        const QString path{directory.filePath(QStringLiteral("voices/ada.wav"))};
        QCOMPARE(IdentityRegistry::load(directory.path()).find(QStringLiteral("ada"))->voicePath,
                 path);

        QFile file{path};
        QVERIFY(file.open(QIODevice::ReadOnly));
        std::vector<float> decoded;
        int rate{0};
        QVERIFY(WavFile::decodeMono(file.readAll(), decoded, rate));
        QCOMPARE(rate, 16000);
        QCOMPARE(decoded.size(), spoken.size());
    }

    void aShortSentenceIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};

        QVERIFY(!library.stageVoiceSamples(sentence(1.0), 16000, QStringLiteral("sentence"), 2.0));
        QVERIFY(library.stagedVoice().isEmpty());
        QVERIFY(!library.problem().isEmpty());

        QVERIFY(library.stageVoiceSamples(sentence(2.0), 16000, QStringLiteral("sentence"), 2.0));
        QVERIFY(!library.stagedVoice().isEmpty());
        QVERIFY(library.problem().isEmpty());
    }

    void aRecordingInAnUnknownContainerIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};

        QVERIFY(!library.stageVoiceData(QByteArrayLiteral("MZ"), QStringLiteral("exe"),
                                        QStringLiteral("tool.exe")));
        QVERIFY(library.stagedVoice().isEmpty());

        QVERIFY(library.stageVoiceData(QByteArrayLiteral("ID3"), QStringLiteral("MP3"),
                                       QStringLiteral("ada.MP3")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, 2));
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("voices/ada.mp3"))));
    }

    void anOversizedFileIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};

        const QByteArray tooLarge(IdentityLibrary::maximumVoiceBytes + 1, '\0');
        QVERIFY(!library.stageVoiceData(tooLarge, QStringLiteral("wav"), QStringLiteral("big.wav")));
        QVERIFY(library.stagedVoice().isEmpty());

        const QByteArray tooLargePicture(IdentityLibrary::maximumFaceBytes + 1, '\0');
        QVERIFY(!library.stageFaceData(tooLargePicture, QStringLiteral("big.png")));
        QVERIFY(library.stagedFace().isEmpty());
    }

    void somethingThatIsNotAPictureIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};

        QVERIFY(!library.stageFaceData(QByteArrayLiteral("not a picture"),
                                       QStringLiteral("notes.png")));
        QVERIFY(library.stagedFace().isEmpty());
        QVERIFY(!library.stageFaceImage(QImage{}, QStringLiteral("camera")));
        QVERIFY(library.stagedFace().isEmpty());
    }

    void removeForgetsThePersonAndTheirFiles()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(library.stageVoiceSamples(sentence(2.5), 16000, QStringLiteral("sentence")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, 2));
        QSignalSpy changed{&library, &IdentityLibrary::galleryChanged};

        QVERIFY(library.remove(QStringLiteral("ada")));

        QCOMPARE(changed.count(), 1);
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("faces/ada.png"))));
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("voices/ada.wav"))));
        QVERIFY(!manifestIn(directory.path()).contains(QStringLiteral("ada")));
        QVERIFY(library.people().isEmpty());
    }

    void removingSomebodyUnknownIsRefused()
    {
        QTemporaryDir directory;
        IdentityLibrary library{directory.path()};
        QSignalSpy changed{&library, &IdentityLibrary::galleryChanged};

        QVERIFY(!library.remove(QStringLiteral("nobody")));
        QVERIFY(!library.problem().isEmpty());
        QCOMPARE(changed.count(), 0);
    }

    void removeNeverDeletesOutsideTheDirectory()
    {
        QTemporaryDir root;
        const QString gallery{root.filePath(QStringLiteral("gallery"))};
        const QString outside{root.filePath(QStringLiteral("outside.png"))};
        writeFile(outside, QByteArrayLiteral("keep me"));
        writeFile(QDir{gallery}.absoluteFilePath(IdentityRegistry::manifestFileName()),
                  QByteArrayLiteral(R"({"eve":{"face":"../outside.png","voice":")")
                      + outside.toUtf8() + QByteArrayLiteral(R"("}})"));

        IdentityLibrary library{gallery};
        QVERIFY(library.remove(QStringLiteral("eve")));

        QVERIFY(QFileInfo::exists(outside));
        QVERIFY(!manifestIn(gallery).contains(QStringLiteral("eve")));
    }

    void aHandWrittenListIsLeftAlone()
    {
        QTemporaryDir directory;
        const QByteArray handWritten{QByteArrayLiteral(R"([{"id":"ada","priority":0}])")};
        writeFile(directory.filePath(IdentityRegistry::manifestFileName()), handWritten);

        IdentityLibrary library{directory.path()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(!library.enrol(QStringLiteral("Bob"), QString{}, 2));
        QVERIFY(!library.problem().isEmpty());

        QFile file{directory.filePath(IdentityRegistry::manifestFileName())};
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), handWritten);
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("faces/bob.png"))));
    }

    void theWritableGalleryIsMergedLast()
    {
        QTemporaryDir shipped;
        writeFile(shipped.filePath(IdentityRegistry::manifestFileName()),
                  QByteArrayLiteral(R"({"bob":{"priority":3}})"));
        {
            QSettings settings;
            settings.setValue(QStringLiteral("Paths/identitiesPath"), shipped.path());
            settings.sync();
        }

        IdentityLibrary library{IdentityDirectory::writableDirectory()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QStringLiteral("owner"), 0));

        const QStringList galleries{IdentityDirectory::galleries()};
        QCOMPARE(galleries,
                 QStringList({QDir{shipped.path()}.absolutePath(),
                              IdentityDirectory::writableDirectory()}));

        const IdentityRegistry registry{IdentityRegistry::load(galleries)};
        QCOMPARE(registry.names(), QStringList({QStringLiteral("ada"), QStringLiteral("bob")}));
    }

    void anEmptyWritableGalleryIsNotListed()
    {
        QTemporaryDir shipped;
        writeFile(shipped.filePath(IdentityRegistry::manifestFileName()),
                  QByteArrayLiteral(R"({"bob":{"priority":3}})"));
        {
            QSettings settings;
            settings.setValue(QStringLiteral("Paths/identitiesPath"), shipped.path());
            settings.sync();
        }
        QDir{}.mkpath(IdentityDirectory::writableDirectory());

        QCOMPARE(IdentityDirectory::galleries(),
                 QStringList({QDir{shipped.path()}.absolutePath()}));
    }

    void theWritableGalleryDoesNotMaskTheShippedOne()
    {
        // Pointed at the writable directory itself, the setting must not let
        // it stand in for the shipped gallery; it is listed once, last.
        IdentityLibrary library{IdentityDirectory::writableDirectory()};
        QVERIFY(library.stageFaceImage(picture(), QStringLiteral("camera")));
        QVERIFY(library.enrol(QStringLiteral("Ada"), QString{}, 2));
        {
            QSettings settings;
            settings.setValue(QStringLiteral("Paths/identitiesPath"),
                              IdentityDirectory::writableDirectory());
            settings.sync();
        }

        const QStringList galleries{IdentityDirectory::galleries()};
        QVERIFY(!galleries.isEmpty());
        QCOMPARE(galleries.last(), IdentityDirectory::writableDirectory());
        QCOMPARE(galleries.count(IdentityDirectory::writableDirectory()), 1);
    }
};

QTEST_GUILESS_MAIN(TestIdentityLibrary)

#include "tst_identitylibrary.moc"
