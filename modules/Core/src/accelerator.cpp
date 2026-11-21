// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "accelerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QLoggingCategory>
#include <QSettings>
#include <QStringList>

#include <array>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(acceleratorLog, "kerfur.accelerator")

constexpr std::size_t errorCapacity{512};

} // namespace

Accelerator &Accelerator::instance()
{
    static Accelerator accelerator;
    return accelerator;
}

Accel::Device *Accelerator::device()
{
    std::call_once(m_opened, [this]() { open(); });
    return m_device.get();
}

std::string Accelerator::problem()
{
    std::call_once(m_opened, [this]() { open(); });
    return m_problem;
}

void Accelerator::open()
{
    const QSettings settings;
    const QString key{QStringLiteral("Perception/accelerator")};
    const QString choice{settings.value(key, QStringLiteral("auto")).toString()};
    if (choice.compare(QStringLiteral("cpu"), Qt::CaseInsensitive) == 0) {
        m_problem = "turned off by Perception/accelerator";
        qCInfo(acceleratorLog) << "accelerator turned off; every stage runs on the CPU";
        return;
    }

    const QString name{QString::fromLatin1(pluginName)};
    std::vector<std::string> candidates;
    if (QCoreApplication::instance() != nullptr) {
        const QDir here{QCoreApplication::applicationDirPath()};
        candidates.push_back(here.absoluteFilePath(name).toStdString());
        candidates.push_back(here.absoluteFilePath(QStringLiteral("../lib/") + name).toStdString());
    }
    candidates.push_back(name.toStdString());

    m_device = openFrom(candidates, m_problem);
    if (m_device == nullptr) {
        qCInfo(acceleratorLog).noquote()
            << QStringLiteral("no accelerator (%1); every stage runs on the CPU")
                   .arg(QString::fromStdString(m_problem));
        return;
    }
    qCInfo(acceleratorLog) << "accelerator" << m_device->description().c_str();
    if (QCoreApplication::instance() != nullptr) {
        qAddPostRoutine(&Accelerator::release);
    }
}

void Accelerator::release()
{
    instance().m_device.reset();
}

std::unique_ptr<Accel::Device> Accelerator::openFrom(const std::vector<std::string> &candidates,
                                                     std::string &problem)
{
    QStringList failures;
    for (const std::string &path : candidates) {
        const QString candidate{QString::fromStdString(path)};
        // Never unloaded: the device it creates lives until exit, and the code
        // behind its vtable must outlive it.
        QLibrary library{candidate};
        if (!library.load()) {
            failures.append(library.errorString());
            continue;
        }
        const auto openDevice{
            reinterpret_cast<KerfurOpenAccelerator>(library.resolve("kerfurOpenAccelerator"))};
        if (openDevice == nullptr) {
            failures.append(QStringLiteral("%1 exports no kerfurOpenAccelerator").arg(candidate));
            continue;
        }

        std::array<char, errorCapacity> error{};
        std::unique_ptr<Accel::Device> device{
            openDevice(Accel::interfaceVersion, error.data(), error.size())};
        if (device == nullptr) {
            problem = library.fileName().toStdString() + ": " + error.data();
            return nullptr;
        }
        problem.clear();
        qCDebug(acceleratorLog) << "plugin" << library.fileName();
        return device;
    }

    problem = "no accelerator plugin";
    qCDebug(acceleratorLog) << failures;
    return nullptr;
}

} // namespace Kerfur
