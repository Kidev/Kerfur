// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "greeter.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(greeterLog, "kerfur.greeter")

namespace Kerfur {

namespace {

constexpr qint64 msPerHour{3'600'000};
constexpr int saveIntervalMs{60'000};

QString absencesKey()
{
    return QStringLiteral("Greetings/absences");
}

QString savedAtKey()
{
    return QStringLiteral("Greetings/savedAt");
}

} // namespace

Greeter::Greeter(QObject *parent) : QObject{parent}
{
    m_elapsed.start();
    m_saveTimer.setInterval(saveIntervalMs);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() {
        if (m_dirty) {
            save();
        }
    });
    m_saveTimer.start();
    load();
}

Greeter::~Greeter()
{
    save();
}

void Greeter::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
}

void Greeter::setGreetAfterHours(qreal hours)
{
    const qreal bounded{std::isfinite(hours) ? std::max(minimumGreetAfterHours, hours)
                                             : m_greetAfterHours};
    if (qFuzzyCompare(m_greetAfterHours, bounded)) {
        return;
    }
    m_greetAfterHours = bounded;
    emit greetAfterHoursChanged();
}

void Greeter::setSleepFactor(qreal factor)
{
    const double before{m_clock.sleepFactor()};
    m_clock.setSleepFactor(factor, now());
    if (!qFuzzyCompare(before, m_clock.sleepFactor())) {
        emit sleepFactorChanged();
    }
}

void Greeter::setAsleep(bool asleep)
{
    if (m_clock.isAsleep() == asleep) {
        return;
    }
    m_clock.setAsleep(asleep, now());
    emit asleepChanged();
}

void Greeter::see(const QStringList &identities)
{
    const qint64 at{now()};
    QSet<QString> counted;
    for (const QString &identity : identities) {
        if (identity.isEmpty() || counted.contains(identity)) {
            continue;
        }
        counted.insert(identity);
        const qint64 away{m_clock.see(identity, at)};
        m_dirty = true;
        if (m_enabled && isReunion(away, greetAfterMs())) {
            qCInfo(greeterLog).noquote()
                << "greeting" << identity << "after"
                << (away < 0 ? QStringLiteral("never being seen")
                             : QStringLiteral("%1 h").arg(static_cast<double>(away) / msPerHour,
                                                          0,
                                                          'f',
                                                          1));
            emit greeted(identity);
        }
    }
}

qint64 Greeter::greetAfterMs() const
{
    return static_cast<qint64>(std::llround(m_greetAfterHours * msPerHour));
}

void Greeter::load()
{
    const QSettings settings;
    const QVariantMap absences{settings.value(absencesKey()).toMap()};
    if (absences.isEmpty()) {
        return;
    }
    // Time with Kerfur closed counts at the waking rate. A clock that went
    // backwards since, as a board without a battery backed clock does until it
    // reaches a time server, adds nothing.
    const qint64 savedAt{settings.value(savedAtKey()).toLongLong()};
    const qint64 offline{savedAt > 0 ? QDateTime::currentMSecsSinceEpoch() - savedAt : 0};
    m_clock.restore(absences, offline, now());
}

void Greeter::save()
{
    QSettings settings;
    settings.setValue(absencesKey(), m_clock.save(now(), greetAfterMs()));
    settings.setValue(savedAtKey(), QDateTime::currentMSecsSinceEpoch());
    m_dirty = false;
}

} // namespace Kerfur
