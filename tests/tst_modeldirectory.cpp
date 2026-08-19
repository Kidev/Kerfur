// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "modeldirectory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QTest>

using namespace Kerfur;

namespace {

constexpr QLatin1StringView settingsKey{"Paths/modelsPath"};

} // namespace

/// The one definition of where model files are looked for.
///
/// Every stage that needs a model resolves through here, so a change to the
/// search order silently relocates the whole perception stack. These cases pin
/// the order and, more importantly, pin that an absent directory reports
/// nothing rather than guessing.
class TestModelDirectory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void candidatesAreNeverEmpty();
    void configuredDirectoryComesFirst();
    void configuredUrlIsAcceptedAsAPath();
    void resolveFindsTheConfiguredDirectory();
    void locateFindsAProvisionedFile();
    void locateReportsNothingForAnAbsentFile();
    void locateReportsNothingWithoutADirectory();
    void searchPathDescriptionNamesTheCandidates();

private:
    void configure(const QString &path);

    QTemporaryDir m_models;
};

void TestModelDirectory::initTestCase()
{
    // Named so that the settings file and the application data location this
    // suite touches are its own and not some other Kerfur build's.
    QCoreApplication::setOrganizationName(QStringLiteral("kidev"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_modeldirectory"));
}

void TestModelDirectory::cleanupTestCase()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void TestModelDirectory::init()
{
    QVERIFY(m_models.isValid());
}

void TestModelDirectory::cleanup()
{
    // The suite writes a real settings file, so every case has to leave it as
    // it found it or the next one inherits an override it never asked for.
    QSettings settings;
    settings.remove(QString{settingsKey});
    settings.sync();
}

void TestModelDirectory::configure(const QString &path)
{
    QSettings settings;
    settings.setValue(QString{settingsKey}, path);
    settings.sync();
}

void TestModelDirectory::candidatesAreNeverEmpty()
{
    // Without a configured override there is still the application data
    // location and the directory beside the executable, so a message about a
    // missing model always has somewhere to name.
    QVERIFY(!ModelDirectory::candidates().isEmpty());
}

void TestModelDirectory::configuredDirectoryComesFirst()
{
    configure(m_models.path());
    QCOMPARE(ModelDirectory::candidates().constFirst(), m_models.path());
}

void TestModelDirectory::configuredUrlIsAcceptedAsAPath()
{
    // What KSettings actually writes: QML's StandardPaths returns urls, so the
    // setting arrives as file:///... . Read back verbatim it names no directory
    // at all, and the override silently does nothing.
    configure(QUrl::fromLocalFile(m_models.path()).toString());

    QCOMPARE(ModelDirectory::candidates().constFirst(), m_models.path());
    QCOMPARE(ModelDirectory::resolve(), QDir{m_models.path()}.absolutePath());
}

void TestModelDirectory::resolveFindsTheConfiguredDirectory()
{
    configure(m_models.path());
    QCOMPARE(ModelDirectory::resolve(), QDir{m_models.path()}.absolutePath());
}

void TestModelDirectory::locateFindsAProvisionedFile()
{
    QFile model{m_models.filePath(QStringLiteral("stage.onnx"))};
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.close();

    configure(m_models.path());
    QCOMPARE(ModelDirectory::locate(QStringLiteral("stage.onnx")), model.fileName());
}

void TestModelDirectory::locateReportsNothingForAnAbsentFile()
{
    // The half provisioned directory. It resolves, so nothing falls through to
    // a second location, and the stage reports as missing.
    configure(m_models.path());
    QVERIFY(!ModelDirectory::resolve().isEmpty());
    QVERIFY(ModelDirectory::locate(QStringLiteral("absent.onnx")).isEmpty());
}

void TestModelDirectory::locateReportsNothingWithoutADirectory()
{
    // The state a fresh clone is in before `make models`: a configured
    // directory that is not there has to be skipped rather than handed back as
    // a path nothing can be read from.
    const QString absent{m_models.filePath(QStringLiteral("does-not-exist"))};
    configure(absent);

    QVERIFY(ModelDirectory::resolve() != absent);
    QVERIFY(!ModelDirectory::locate(QStringLiteral("stage.onnx")).startsWith(absent));
}

void TestModelDirectory::searchPathDescriptionNamesTheCandidates()
{
    configure(m_models.path());
    QVERIFY(ModelDirectory::describeSearchPath().contains(m_models.path()));
}

QTEST_MAIN(TestModelDirectory)

#include "tst_modeldirectory.moc"
