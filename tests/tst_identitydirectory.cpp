// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitydirectory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Kerfur;

namespace {

void writeFile(const QString &path)
{
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QFile file{path};
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write("x");
}

} // namespace

class TestIdentityDirectory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Its own organisation and application, so that the suite cannot read
        // or write the settings of a real installation on the machine running
        // it.
        QCoreApplication::setOrganizationName(QStringLiteral("kidev"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_identitydirectory"));
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void init()
    {
        QSettings settings;
        settings.remove(QStringLiteral("Paths/identitiesPath"));
        settings.sync();
    }

    void theConfiguredPathComesFirst()
    {
        QTemporaryDir gallery;
        QVERIFY(gallery.isValid());
        writeFile(gallery.filePath(QStringLiteral("kidev.png")));

        QSettings settings;
        settings.setValue(QStringLiteral("Paths/identitiesPath"), gallery.path());
        settings.sync();

        QCOMPARE(IdentityDirectory::candidates().first(), QDir::cleanPath(gallery.path()));
        QCOMPARE(IdentityDirectory::resolve(), QDir{gallery.path()}.absolutePath());
    }

    /// QML's StandardPaths hands back urls, so the setting reaches C++ as
    /// `file:///...`, which QDir cannot open. The url has to be understood
    /// here or the override silently resolves to nothing.
    void aFileUrlIsAcceptedAsAPath()
    {
        QTemporaryDir gallery;
        QVERIFY(gallery.isValid());
        writeFile(gallery.filePath(QStringLiteral("kidev.mp3")));

        QSettings settings;
        settings.setValue(QStringLiteral("Paths/identitiesPath"),
                          QStringLiteral("file://") + gallery.path());
        settings.sync();

        QCOMPARE(IdentityDirectory::resolve(), QDir{gallery.path()}.absolutePath());
    }

    /// An empty directory left behind by an uninstall must not mask a
    /// populated one further down the list. "Nobody is enrolled" and "the
    /// wrong directory was found" are different problems.
    void anEmptyDirectoryIsNotAGallery()
    {
        QTemporaryDir empty;
        QVERIFY(empty.isValid());

        QSettings settings;
        settings.setValue(QStringLiteral("Paths/identitiesPath"), empty.path());
        settings.sync();

        QVERIFY(IdentityDirectory::resolve() != QDir{empty.path()}.absolutePath());
    }

    void aMissingDirectoryResolvesToNothing()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("Paths/identitiesPath"),
                          QStringLiteral("/nonexistent/kerfur/identities"));
        settings.sync();

        // Nothing else on the machine running this is expected to hold a
        // gallery either, so an empty answer is the honest one.
        const QString resolved{IdentityDirectory::resolve()};
        QVERIFY(resolved != QStringLiteral("/nonexistent/kerfur/identities"));
    }

    /// One person is a face and a voice sharing a stem, and either half alone
    /// still enrols them.
    void bothHalvesOfAnEnrolmentShareOneName()
    {
        QTemporaryDir gallery;
        QVERIFY(gallery.isValid());
        writeFile(gallery.filePath(QStringLiteral("kidev.png")));
        writeFile(gallery.filePath(QStringLiteral("kidev.mp3")));
        writeFile(gallery.filePath(QStringLiteral("someone.wav")));
        writeFile(gallery.filePath(QStringLiteral("notes.txt")));

        const QStringList names{IdentityDirectory::enrolledNames(gallery.path())};
        QCOMPARE(names, QStringList({QStringLiteral("kidev"), QStringLiteral("someone")}));
    }

    void namesOfNowhereAreEmpty()
    {
        QVERIFY(IdentityDirectory::enrolledNames(QString{}).isEmpty());
        QVERIFY(IdentityDirectory::enrolledNames(QStringLiteral("/nonexistent")).isEmpty());
    }

    void theSearchPathIsReportable()
    {
        const QString description{IdentityDirectory::describeSearchPath()};
        QVERIFY(!description.isEmpty());
        QVERIFY(description.contains(QStringLiteral("identities")));
    }

    void candidatesAreNotRepeated()
    {
        const QStringList candidates{IdentityDirectory::candidates()};
        QStringList unique{candidates};
        unique.removeDuplicates();
        QCOMPARE(candidates, unique);
    }
};

QTEST_MAIN(TestIdentityDirectory)
#include "tst_identitydirectory.moc"
