// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ABSENCECLOCK_H
#define KERFUR_ABSENCECLOCK_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Kerfur {

/// How long each person has been out of sight.
///
/// Absences are measured in waking time. While Kerfur is asleep (watching for
/// motion with its models stopped) every millisecond counts `sleepFactor` times
/// over, so a night spent asleep is long however short it was. Times are
/// milliseconds on any monotonic clock the caller keeps; absences are
/// milliseconds of waking time.
class AbsenceClock
{
public:
    /// How much faster time runs asleep, from `now` on. Held at 1 at least.
    void setSleepFactor(double factor, qint64 now);
    double sleepFactor() const { return m_sleepFactor; }

    /// From `now` on, time runs at the asleep rate while `asleep`.
    void setAsleep(bool asleep, qint64 now);
    bool isAsleep() const { return m_asleep; }

    /// How long `identity` has been out of sight at `now`, or -1 for somebody
    /// this clock has never seen.
    qint64 absence(const QString &identity, qint64 now) const;

    /// `identity` is in view at `now`. Returns how long they had been out of
    /// sight, or -1 for a first sighting, and starts their absence again.
    qint64 see(const QString &identity, qint64 now);

    /// Everybody's absence at `now`, by identity, leaving out the ones at or
    /// past `keepBelow`: those read the same as never seen.
    QVariantMap save(qint64 now, qint64 keepBelow) const;

    /// Puts back what `save` wrote, as of `now`, adding `offline` to every
    /// absence: the waking time that passed while nothing was measuring. Only
    /// entries holding a whole number of milliseconds are read.
    void restore(const QVariantMap &saved, qint64 offline, qint64 now);

    QStringList identities() const { return m_seenAt.keys(); }

private:
    /// The waking time elapsed from the start of this clock to `now`.
    double wakingTime(qint64 now) const;
    /// Folds the time up to `now` into `m_wakingAtMark` before a rate changes.
    void mark(qint64 now);

    double m_sleepFactor{4.0};
    bool m_asleep{false};
    /// When the rate last changed, and the waking time reached by then.
    qint64 m_mark{0};
    double m_wakingAtMark{0.0};
    /// The waking time of each person's last sighting.
    QHash<QString, double> m_seenAt;
};

/// Whether somebody seen again after `absence` (-1 for never) is greeted.
inline bool isReunion(qint64 absence, qint64 greetAfter)
{
    return absence < 0 || absence >= greetAfter;
}

} // namespace Kerfur

#endif // KERFUR_ABSENCECLOCK_H
