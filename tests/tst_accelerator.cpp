// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "accelerator.h"

#include <QLibraryInfo>
#include <QTest>

#include <memory>
#include <string>
#include <vector>

using namespace Kerfur;

/// The search for an accelerator plugin: the device when one loads, nothing
/// otherwise, and a reason either way. A machine without the hardware is the
/// common case, and it must end in the CPU graph rather than a failed start.
class TestAccelerator : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void opensThePlugin();
    void refusalEndsTheSearch();
    void libraryWithoutTheSymbolIsSkipped();
    void nothingFound();
};

void TestAccelerator::cleanup()
{
    qunsetenv("KERFUR_FAKE_ACCELERATOR_REFUSES");
}

void TestAccelerator::opensThePlugin()
{
    std::string problem{"stale"};
    const std::unique_ptr<Accel::Device> device{
        Accelerator::openFrom({"/nonexistent/kerfur-hailo", KERFUR_FAKE_ACCELERATOR}, problem)};
    QVERIFY(device != nullptr);
    QCOMPARE(device->description(), std::string{"fake device"});
    QVERIFY(problem.empty());
}

void TestAccelerator::refusalEndsTheSearch()
{
    qputenv("KERFUR_FAKE_ACCELERATOR_REFUSES", "no device on the bus");
    std::string problem;
    const std::unique_ptr<Accel::Device> device{
        Accelerator::openFrom({KERFUR_FAKE_ACCELERATOR, KERFUR_FAKE_ACCELERATOR}, problem)};
    QVERIFY(device == nullptr);
    QVERIFY2(problem.find("no device on the bus") != std::string::npos, problem.c_str());
}

void TestAccelerator::libraryWithoutTheSymbolIsSkipped()
{
    // Any shared library loads; QtCore exports no kerfurOpenAccelerator.
    const std::string qtCore{
        (QLibraryInfo::path(QLibraryInfo::LibrariesPath) + QStringLiteral("/libQt6Core.so.6"))
            .toStdString()};
    std::string problem;
    const std::unique_ptr<Accel::Device> device{
        Accelerator::openFrom({qtCore, KERFUR_FAKE_ACCELERATOR}, problem)};
    QVERIFY(device != nullptr);
}

void TestAccelerator::nothingFound()
{
    std::string problem;
    const std::unique_ptr<Accel::Device> device{
        Accelerator::openFrom({"/nonexistent/kerfur-hailo"}, problem)};
    QVERIFY(device == nullptr);
    QCOMPARE(problem, std::string{"no accelerator plugin"});
}

QTEST_GUILESS_MAIN(TestAccelerator)

#include "tst_accelerator.moc"
