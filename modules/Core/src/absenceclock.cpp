// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "absenceclock.h"

#include <algorithm>
#include <cmath>

namespace Kerfur {

void AbsenceClock::setSleepFactor(double factor, qint64 now)
{
    mark(now);
    m_sleepFactor = std::isfinite(factor) ? std::max(1.0, factor) : 1.0;
}

void AbsenceClock::setAsleep(bool asleep, qint64 now)
{
    mark(now);
    m_asleep = asleep;
}

qint64 AbsenceClock::absence(const QString &identity, qint64 now) const
{
    const auto found{m_seenAt.constFind(identity)};
    if (found == m_seenAt.cend()) {
        return -1;
    }
    return std::max<qint64>(0, std::llround(wakingTime(now) - found.value()));
}

qint64 AbsenceClock::see(const QString &identity, qint64 now)
{
    const qint64 away{absence(identity, now)};
    m_seenAt.insert(identity, wakingTime(now));
    return away;
}

QVariantMap AbsenceClock::save(qint64 now, qint64 keepBelow) const
{
    QVariantMap saved;
    for (auto entry{m_seenAt.cbegin()}; entry != m_seenAt.cend(); ++entry) {
        const qint64 away{absence(entry.key(), now)};
        if (away < keepBelow) {
            saved.insert(entry.key(), QVariant::fromValue<qlonglong>(away));
        }
    }
    return saved;
}

void AbsenceClock::restore(const QVariantMap &saved, qint64 offline, qint64 now)
{
    const double reached{wakingTime(now)};
    const qint64 unmeasured{std::max<qint64>(0, offline)};
    for (auto entry{saved.cbegin()}; entry != saved.cend(); ++entry) {
        bool isNumber{false};
        const qlonglong away{entry.value().toLongLong(&isNumber)};
        if (!isNumber || away < 0) {
            continue;
        }
        m_seenAt.insert(entry.key(), reached - static_cast<double>(away + unmeasured));
    }
}

double AbsenceClock::wakingTime(qint64 now) const
{
    const double elapsed{static_cast<double>(std::max<qint64>(0, now - m_mark))};
    return m_wakingAtMark + (elapsed * (m_asleep ? m_sleepFactor : 1.0));
}

void AbsenceClock::mark(qint64 now)
{
    m_wakingAtMark = wakingTime(now);
    m_mark = std::max(m_mark, now);
}

} // namespace Kerfur
