// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "settingspath.h"

#include <QTest>
#include <QUrl>

using namespace Kerfur;

/// The seam between the two ways a directory reaches C++.
///
/// KSettings derives its paths from QML's StandardPaths, which returns urls, so
/// the configuration file holds `file:///...` while every consumer wants a path
/// QDir can open. A regression here disables the model directory and the face
/// gallery at once, and does it silently.
class TestSettingsPath : public QObject
{
    Q_OBJECT

private slots:
    void emptyStaysEmpty();
    void plainPathIsKept();
    void fileUrlBecomesAPath();
    void percentEncodingIsDecoded();
    void windowsPathIsNotMistakenForAUrl();
    void trailingSeparatorIsRemoved();
};

void TestSettingsPath::emptyStaysEmpty()
{
    QVERIFY(settingsDirectoryPath(QString{}).isEmpty());
}

void TestSettingsPath::plainPathIsKept()
{
    QCOMPARE(settingsDirectoryPath(QStringLiteral("/opt/kerfur/models")),
             QStringLiteral("/opt/kerfur/models"));
}

void TestSettingsPath::fileUrlBecomesAPath()
{
    QCOMPARE(settingsDirectoryPath(QStringLiteral("file:///opt/kerfur/models")),
             QStringLiteral("/opt/kerfur/models"));
}

void TestSettingsPath::percentEncodingIsDecoded()
{
    const QString path{QStringLiteral("/opt/kerfur models")};
    QCOMPARE(settingsDirectoryPath(QUrl::fromLocalFile(path).toString()), path);
}

void TestSettingsPath::windowsPathIsNotMistakenForAUrl()
{
    // QUrl parses this as scheme "c", so an unconditional round trip would
    // hand back an empty string and the override would vanish.
    QCOMPARE(settingsDirectoryPath(QStringLiteral("C:/kerfur/models")),
             QStringLiteral("C:/kerfur/models"));
}

void TestSettingsPath::trailingSeparatorIsRemoved()
{
    // KSettings appends one, and it has to compare equal to the same directory
    // named without it.
    QCOMPARE(settingsDirectoryPath(QStringLiteral("file:///opt/kerfur/models/")),
             settingsDirectoryPath(QStringLiteral("/opt/kerfur/models")));
}

QTEST_MAIN(TestSettingsPath)

#include "tst_settingspath.moc"
