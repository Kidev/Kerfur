// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_GREETER_H
#define KERFUR_GREETER_H

#include "absenceclock.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// Says when somebody Kerfur knows comes back after a long time out of sight.
///
/// The face reports who is in view on every pass, and `greeted` fires for each
/// person seen for the first time, or for the first time in `greetAfterHours`
/// of waking time. Time asleep runs `sleepFactor` times faster (see
/// AbsenceClock), and time with Kerfur closed counts once.
///
/// Absences are kept in the settings, group `Greetings`, and written once a
/// minute while somebody is in view, and when the object goes away. Only people
/// seen within `greetAfterHours` are written: anybody else reads as never seen,
/// which greets them all the same.
class Greeter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// Whether a return is greeted. Sightings are counted either way, so
    /// turning this on greets nobody who was in view all along.
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    /// Hours of waking time out of sight after which a person is greeted.
    Q_PROPERTY(qreal greetAfterHours READ greetAfterHours WRITE setGreetAfterHours NOTIFY
                   greetAfterHoursChanged)
    /// How many times faster time runs while Kerfur is asleep.
    Q_PROPERTY(qreal sleepFactor READ sleepFactor WRITE setSleepFactor NOTIFY sleepFactorChanged)
    /// Whether Kerfur is asleep: watching for motion, its models stopped.
    Q_PROPERTY(bool asleep READ isAsleep WRITE setAsleep NOTIFY asleepChanged)

public:
    /// The shortest `greetAfterHours`: three minutes, well above the gap
    /// between two passes, so somebody in view is never greeted twice in a row.
    static constexpr qreal minimumGreetAfterHours{0.05};

    explicit Greeter(QObject *parent = nullptr);
    ~Greeter() override;

    bool isEnabled() const { return m_enabled; }
    qreal greetAfterHours() const { return m_greetAfterHours; }
    qreal sleepFactor() const { return m_clock.sleepFactor(); }
    bool isAsleep() const { return m_clock.isAsleep(); }

    void setEnabled(bool enabled);
    void setGreetAfterHours(qreal hours);
    void setSleepFactor(qreal factor);
    void setAsleep(bool asleep);

    /// The people in view now, by identity. Empty names are ignored, and a
    /// name listed twice counts once.
    Q_INVOKABLE void see(const QStringList &identities);

signals:
    void enabledChanged();
    void greetAfterHoursChanged();
    void sleepFactorChanged();
    void asleepChanged();
    /// `identity` came back after `greetAfterHours` or more, or was seen for
    /// the first time.
    void greeted(const QString &identity);

private:
    qint64 now() const { return m_elapsed.elapsed(); }
    qint64 greetAfterMs() const;
    void load();
    void save();

    AbsenceClock m_clock;
    QElapsedTimer m_elapsed;
    QTimer m_saveTimer;
    qreal m_greetAfterHours{8.0};
    bool m_enabled{true};
    bool m_dirty{false};
};

} // namespace Kerfur

#endif // KERFUR_GREETER_H
