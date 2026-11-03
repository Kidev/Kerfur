// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SKILL_H
#define KERFUR_SKILL_H

#include "identityregistry.h"

#include <QDateTime>
#include <QJSValue>
#include <QList>
#include <QObject>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

class Skill;

/// Something declared inside a Skill. Knows which one, so that a trigger can
/// reach its skill's helpers without the author threading an id through.
class SkillItem : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    /// Off switches everything this item does, without deleting it. A routine
    /// that should only run at night and a trigger that should only work when
    /// somebody is home are both this.
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
    using QObject::QObject;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    Skill *skill() const { return m_skill; }
    virtual void attach(Skill *skill);

signals:
    void enabledChanged();

protected:
    Skill *m_skill{nullptr};
    bool m_enabled{true};
};

/// One thing a skill reacts to being asked.
///
///     Trigger {
///         intent: "when asked about adding calories"
///         phrases: ["add {amount} calories", "i ate {amount} calories"]
///         onFired: (slots) => day.add("caloriesOfTheDay", slots.amount)
///     }
///
/// `intent` is both the identity of the trigger and the sentence a language
/// model would be shown; `phrases` are the exact forms the local matcher
/// recognises. Writing only `intent` works and is looser; writing phrases as
/// well is what makes a slot reliable, because a slot has to be somewhere in a
/// pattern to be extracted from a sentence.
class Trigger : public SkillItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString intent READ intent WRITE setIntent NOTIFY intentChanged)
    Q_PROPERTY(QStringList phrases READ phrases WRITE setPhrases NOTIFY phrasesChanged)

    /// Asked before acting, with `{slot}` and `<variable>` expanded. Empty
    /// means act at once.
    ///
    /// The confirmation is answered by the next thing the same person says, and
    /// by nobody else: a second voice saying "yes" while the first is being
    /// asked is not an answer to the question.
    Q_PROPERTY(QString confirm READ confirm WRITE setConfirm NOTIFY confirmChanged)

    /// The lowest rank allowed to fire this. Zero -- only the top ranked person
    /// because that is the safe default for anything that acts on the world.
    /// A trigger that merely answers a question can raise it, and
    /// `Trigger.Anyone` opens it to strangers.
    Q_PROPERTY(int authority READ authority WRITE setAuthority NOTIFY authorityChanged)

public:
    using SkillItem::SkillItem;

    /// For `authority: Trigger.Anyone`.
    enum Rank : int { TopRank = 0, Anyone = IdentityRegistry::unknownPriority };
    Q_ENUM(Rank)

    QString intent() const { return m_intent; }
    void setIntent(const QString &intent);

    QStringList phrases() const { return m_phrases; }
    void setPhrases(const QStringList &phrases);

    QString confirm() const { return m_confirm; }
    void setConfirm(const QString &confirm);

    int authority() const { return m_authority; }
    void setAuthority(int authority);

    /// The unique id this trigger is matched and routed by: the skill's name
    /// and the intent, so that two skills may use the same wording.
    QString key() const;

signals:
    void intentChanged();
    void phrasesChanged();
    void confirmChanged();
    void authorityChanged();

    /// What the author writes `onFired` for. The first argument holds
    /// whatever the patterns captured, already converted to numbers where they
    /// read as numbers. It is not called `slots` in the C++, because that is a
    /// Qt keyword macro; a QML arrow handler names its own parameters and may
    /// call it whatever reads best.
    void fired(const QVariantMap &captures, const QString &speaker);

    /// Somebody who is not allowed to gave this order. Separate from `fired`
    /// rather than a flag on it, so that ignoring the distinction is not
    /// possible by accident.
    void refused(const QString &speaker, int priority);

private:
    QString m_intent;
    QStringList m_phrases;
    QString m_confirm;
    int m_authority{TopRank};
};

/// Something a skill does on its own, regardless of what anybody says.
///
///     Routine {
///         dailyAt: "00:00"
///         onTriggered: day.set("caloriesOfTheDay", 0)
///     }
///
/// `dailyAt` is wall clock and re-arms itself against the clock rather than by
/// adding twenty four hours, so it does not drift and does not skip when the
/// machine was asleep across midnight.
class Routine : public SkillItem, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)

    /// Milliseconds between runs. Zero means never.
    Q_PROPERTY(int every READ every WRITE setEvery NOTIFY everyChanged)

    /// A wall clock time, "HH:mm". Takes precedence over `every`.
    Q_PROPERTY(QString dailyAt READ dailyAt WRITE setDailyAt NOTIFY dailyAtChanged)

    /// Also run once when the skill loads. For a routine that establishes a
    /// value rather than changing one.
    Q_PROPERTY(bool runOnStart READ runsOnStart WRITE setRunOnStart NOTIFY runOnStartChanged)

    /// When it will next run, for the panel. Invalid when it will not.
    Q_PROPERTY(QDateTime nextRun READ nextRun NOTIFY nextRunChanged)

public:
    explicit Routine(QObject *parent = nullptr);

    int every() const { return m_every; }
    void setEvery(int every);

    QString dailyAt() const { return m_dailyAt; }
    void setDailyAt(const QString &dailyAt);

    bool runsOnStart() const { return m_runOnStart; }
    void setRunOnStart(bool runOnStart);

    QDateTime nextRun() const { return m_nextRun; }

    void classBegin() override {}
    void componentComplete() override;

signals:
    void everyChanged();
    void dailyAtChanged();
    void runOnStartChanged();
    void nextRunChanged();
    void triggered();

private:
    void rearm();
    void fire();

    QTimer m_timer;
    QString m_dailyAt;
    QDateTime m_nextRun;
    int m_every{0};
    bool m_runOnStart{false};
    bool m_complete{false};
};

/// Reacts to one variable changing.
///
///     Watch {
///         key: "caloriesOfTheDay"
///         onChanged: (value) => if (value > day.number("maxCaloriesPerDay")) ...
///     }
///
/// The same thing can be written as a QML binding --
/// `Blackboard.values.caloriesOfTheDay` -- and for anything that computes a
/// value that is the better form. This exists for the other case: reacting to
/// a change with an action rather than with a value.
class Watch : public SkillItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY keyChanged)

public:
    using SkillItem::SkillItem;

    QString key() const { return m_key; }
    void setKey(const QString &key);

    void attach(Skill *skill) override;

signals:
    void keyChanged();
    void changed(const QVariant &value);

private:
    QString m_key;
};

/// Declares a variable, its starting value and whether it survives a restart.
///
/// Optional -- writing to an undeclared name works -- but a declared variable
/// can be reset to something, appears in the panel before it has ever been
/// written, and says in the skill file what the skill's state actually is.
class Variable : public SkillItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY keyChanged)
    Q_PROPERTY(QVariant defaultValue READ defaultValue WRITE setDefaultValue NOTIFY
                   defaultValueChanged)
    Q_PROPERTY(bool persistent READ isPersistent WRITE setPersistent NOTIFY persistentChanged)

public:
    using SkillItem::SkillItem;

    QString key() const { return m_key; }
    void setKey(const QString &key);

    QVariant defaultValue() const { return m_defaultValue; }
    void setDefaultValue(const QVariant &value);

    bool isPersistent() const { return m_persistent; }
    void setPersistent(bool persistent);

    void attach(Skill *skill) override;

signals:
    void keyChanged();
    void defaultValueChanged();
    void persistentChanged();

private:
    QString m_key;
    QVariant m_defaultValue;
    bool m_persistent{true};
};

/// One scripted behaviour, loaded from a `.qml` file at run time.
///
/// A skill is not compiled in. It lives in `skills/<name>.qml` next to the
/// identities, is read at startup, and can be edited without rebuilding
/// anything -- which is the entire requirement: adding a way to respond to
/// something has to be a file, not a patch.
///
/// What a skill may do is deliberately narrow. It reacts to being asked
/// something, it reacts to the clock, it reacts to a variable changing, and it
/// can speak, set variables and raise named events. Everything beyond that --
/// calling a house automation system, driving a light -- happens by raising an
/// event that something else is listening for, so that a skill file can never
/// be the reason the robot cannot start.
class Skill : public SkillItem, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_CLASSINFO("DefaultProperty", "contents")

    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QQmlListProperty<QObject> contents READ contents)
    Q_PROPERTY(QStringList intents READ intents NOTIFY intentsChanged)

public:
    explicit Skill(QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);

    QString description() const { return m_description; }
    void setDescription(const QString &description);

    QQmlListProperty<QObject> contents();
    QStringList intents() const;

    QList<Trigger *> triggers() const { return m_triggers; }

    void classBegin() override {}
    void componentComplete() override;

    /// Says something out loud, once Kerfur can speak. Today it reaches the
    /// panel and the `spoke` signal; the template is expanded first, so
    /// `say("You are at <caloriesOfTheDay>")` is the whole of the common case.
    Q_INVOKABLE void say(const QString &text, const QVariantMap &extra = {});

    /// The variables, through this skill rather than through the singleton, so
    /// that a skill file reads as one object talking to itself.
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE double number(const QString &key, double fallback = 0.0) const;
    Q_INVOKABLE QString text(const QString &key, const QString &fallback = {}) const;
    Q_INVOKABLE double add(const QString &key, double delta);
    Q_INVOKABLE void reset(const QString &key);
    Q_INVOKABLE QString expand(const QString &tmpl, const QVariantMap &extra = {}) const;

    /// Asks a question and runs `onConfirmed` only if the same person says yes.
    /// The mechanism `Trigger.confirm` uses, exposed because a skill sometimes
    /// wants to confirm something it decided rather than something it was told.
    Q_INVOKABLE void ask(const QString &question, const QJSValue &onConfirmed);

    /// Raises a named event for whatever is listening -- the automation bridge,
    /// the panel, another skill. The one way out of the sandbox.
    Q_INVOKABLE void emitEvent(const QString &event, const QVariantMap &payload = {});

signals:
    void nameChanged();
    void descriptionChanged();
    void intentsChanged();

private:
    static void appendContent(QQmlListProperty<QObject> *list, QObject *object);
    static qsizetype contentCount(QQmlListProperty<QObject> *list);
    static QObject *contentAt(QQmlListProperty<QObject> *list, qsizetype index);
    static void clearContents(QQmlListProperty<QObject> *list);

    QString m_name;
    QString m_description;
    QList<QObject *> m_contents;
    QList<Trigger *> m_triggers;
};

} // namespace Kerfur

#endif // KERFUR_SKILL_H
