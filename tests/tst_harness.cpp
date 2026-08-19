// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QTest>

/// Proves the CTest wiring reaches a real test binary. Everything else in
/// tests/ assumes this works, so when a whole suite goes quiet this is the
/// case that says whether the harness or the code is at fault.
class TestHarness : public QObject
{
    Q_OBJECT

private slots:
    void runsAtAll();
};

void TestHarness::runsAtAll()
{
    QCOMPARE(1 + 1, 2);
}

QTEST_APPLESS_MAIN(TestHarness)
#include "tst_harness.moc"
