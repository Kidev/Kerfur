// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "skill.h"

#include "blackboard.h"
#include "skillregistry.h"

#include <QLoggingCategory>
#include <QTime>

#include <algorithm>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(skillLog, "kerfur.skills")

} // namespace

void SkillItem::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
}

void SkillItem::attach(Skill *skill)
{
    m_skill = skill;
}

// --- Trigger ---------------------------------------------------------------

void Trigger::setIntent(const QString &intent)
{
    if (m_intent == intent) {
        return;
    }
    m_intent = intent;
    emit intentChanged();
}

void Trigger::setPhrases(const QStringList &phrases)
{
    if (m_phrases == phrases) {
        return;
    }
    m_phrases = phrases;
    emit phrasesChanged();
}

void Trigger::setConfirm(const QString &confirm)
{
    if (m_confirm == confirm) {
        return;
    }
    m_confirm = confirm;
    emit confirmChanged();
}

void Trigger::setAuthority(int authority)
{
    const int clamped{std::max(0, authority)};
    if (m_authority == clamped) {
        return;
    }
    m_authority = clamped;
    emit authorityChanged();
}

QString Trigger::key() const
{
    const QString owner{m_skill != nullptr ? m_skill->name() : QStringLiteral("?")};
    return owner + QStringLiteral("::") + m_intent;
}

// --- Routine ---------------------------------------------------------------

Routine::Routine(QObject *parent) : SkillItem{parent}, m_timer{this}
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Routine::fire);
    connect(this, &SkillItem::enabledChanged, this, &Routine::rearm);
}

void Routine::setEvery(int every)
{
    const int clamped{std::max(0, every)};
    if (m_every == clamped) {
        return;
    }
    m_every = clamped;
    emit everyChanged();
    rearm();
}

void Routine::setDailyAt(const QString &dailyAt)
{
    if (m_dailyAt == dailyAt) {
        return;
    }
    m_dailyAt = dailyAt;
    emit dailyAtChanged();
    rearm();
}

void Routine::setRunOnStart(bool runOnStart)
{
    if (m_runOnStart == runOnStart) {
        return;
    }
    m_runOnStart = runOnStart;
    emit runOnStartChanged();
}

void Routine::componentComplete()
{
    m_complete = true;
    rearm();
    if (m_runOnStart && m_enabled) {
        emit triggered();
    }
}

void Routine::rearm()
{
    if (!m_complete) {
        return;
    }

    m_timer.stop();
    QDateTime next;

    if (!m_dailyAt.isEmpty()) {
        const QTime when{QTime::fromString(m_dailyAt, QStringLiteral("HH:mm"))};
        if (!when.isValid()) {
            qCWarning(skillLog) << "dailyAt" << m_dailyAt << "is not HH:mm; routine disabled";
        } else {
            // Against the clock rather than by adding a day to the last run.
            // A machine that was asleep over midnight comes back and runs at
            // the next occurrence, not twenty four hours after whenever it woke
            // up, and a machine that never sleeps does not drift by the few
            // milliseconds a timer costs each day.
            const QDateTime now{QDateTime::currentDateTime()};
            next = QDateTime{now.date(), when};
            if (next <= now) {
                next = next.addDays(1);
            }
        }
    } else if (m_every > 0) {
        next = QDateTime::currentDateTime().addMSecs(m_every);
    }

    if (m_nextRun != next) {
        m_nextRun = next;
        emit nextRunChanged();
    }

    if (!m_enabled || !next.isValid()) {
        return;
    }

    const qint64 delay{QDateTime::currentDateTime().msecsTo(next)};
    // QTimer takes an int, and a daily routine armed just after it fired is
    // under twenty five days, so the clamp is a formality rather than a
    // behaviour. It is here because the alternative is a silent overflow into a
    // negative interval, which fires immediately and loops.
    m_timer.start(static_cast<int>(std::clamp<qint64>(delay, 0, 24 * 60 * 60 * 1000)));
}

void Routine::fire()
{
    if (m_enabled) {
        emit triggered();
    }
    rearm();
}

// --- Watch -----------------------------------------------------------------

void Watch::setKey(const QString &key)
{
    if (m_key == key) {
        return;
    }
    m_key = key;
    emit keyChanged();
}

void Watch::attach(Skill *skill)
{
    SkillItem::attach(skill);
    connect(&Blackboard::instance(),
            &Blackboard::changed,
            this,
            [this](const QString &key, const QVariant &value) {
                if (m_enabled && !m_key.isEmpty() && key == m_key) {
                    emit changed(value);
                }
            });
}

// --- Variable --------------------------------------------------------------

void Variable::setKey(const QString &key)
{
    if (m_key == key) {
        return;
    }
    m_key = key;
    emit keyChanged();
}

void Variable::setDefaultValue(const QVariant &value)
{
    if (m_defaultValue == value) {
        return;
    }
    m_defaultValue = value;
    emit defaultValueChanged();
}

void Variable::setPersistent(bool persistent)
{
    if (m_persistent == persistent) {
        return;
    }
    m_persistent = persistent;
    emit persistentChanged();
}

void Variable::attach(Skill *skill)
{
    SkillItem::attach(skill);
    if (!m_key.isEmpty()) {
        Blackboard::instance().declare(m_key, m_defaultValue, m_persistent);
    }
}

// --- Skill -----------------------------------------------------------------

Skill::Skill(QObject *parent) : SkillItem{parent} {}

void Skill::setName(const QString &name)
{
    if (m_name == name) {
        return;
    }
    m_name = name;
    emit nameChanged();
}

void Skill::setDescription(const QString &description)
{
    if (m_description == description) {
        return;
    }
    m_description = description;
    emit descriptionChanged();
}

QQmlListProperty<QObject> Skill::contents()
{
    return QQmlListProperty<QObject>{this,
                                     this,
                                     &Skill::appendContent,
                                     &Skill::contentCount,
                                     &Skill::contentAt,
                                     &Skill::clearContents};
}

void Skill::appendContent(QQmlListProperty<QObject> *list, QObject *object)
{
    auto *skill{static_cast<Skill *>(list->data)};
    if (skill == nullptr || object == nullptr) {
        return;
    }
    skill->m_contents.append(object);
    // Parented so that the skill owns everything declared inside it and one
    // delete takes the whole thing down. QML's own ownership of a component's
    // root does not extend to objects reached through a custom list property.
    object->setParent(skill);
}

qsizetype Skill::contentCount(QQmlListProperty<QObject> *list)
{
    return static_cast<Skill *>(list->data)->m_contents.size();
}

QObject *Skill::contentAt(QQmlListProperty<QObject> *list, qsizetype index)
{
    return static_cast<Skill *>(list->data)->m_contents.value(index);
}

void Skill::clearContents(QQmlListProperty<QObject> *list)
{
    static_cast<Skill *>(list->data)->m_contents.clear();
}

void Skill::componentComplete()
{
    if (m_name.isEmpty()) {
        // A skill without a name cannot be routed to, because a trigger's key
        // is its skill's name and its intent. Named after nothing is better
        // than silently sharing a key with the next unnamed skill.
        m_name = QStringLiteral("unnamed");
        emit nameChanged();
    }

    // Attached here rather than as each child arrives, because a Variable's
    // key and default are not set until its own component is complete, and
    // declaring it earlier would declare it under an empty name.
    for (QObject *object : std::as_const(m_contents)) {
        if (auto *item{qobject_cast<SkillItem *>(object)}) {
            item->attach(this);
        }
        if (auto *trigger{qobject_cast<Trigger *>(object)}) {
            if (trigger->intent().isEmpty()) {
                qCWarning(skillLog) << m_name << "has a trigger with no intent; ignored";
                continue;
            }
            m_triggers.append(trigger);
        }
    }

    emit intentsChanged();
    SkillRegistry::instance().registerSkill(this);
}

QStringList Skill::intents() const
{
    QStringList names;
    names.reserve(m_triggers.size());
    for (const Trigger *trigger : m_triggers) {
        names.append(trigger->intent());
    }
    return names;
}

void Skill::say(const QString &text, const QVariantMap &extra)
{
    SkillRegistry::instance().say(Blackboard::instance().expand(text, extra), m_name);
}

void Skill::set(const QString &key, const QVariant &value)
{
    Blackboard::instance().set(key, value);
}

QVariant Skill::value(const QString &key, const QVariant &fallback) const
{
    return Blackboard::instance().value(key, fallback);
}

double Skill::number(const QString &key, double fallback) const
{
    return Blackboard::instance().number(key, fallback);
}

QString Skill::text(const QString &key, const QString &fallback) const
{
    return Blackboard::instance().text(key, fallback);
}

double Skill::add(const QString &key, double delta)
{
    return Blackboard::instance().add(key, delta);
}

void Skill::reset(const QString &key)
{
    Blackboard::instance().reset(key);
}

QString Skill::expand(const QString &tmpl, const QVariantMap &extra) const
{
    return Blackboard::instance().expand(tmpl, extra);
}

void Skill::ask(const QString &question, const QJSValue &onConfirmed)
{
    SkillRegistry::instance().requestConfirmation(Blackboard::instance().expand(question),
                                                  onConfirmed,
                                                  QString{},
                                                  m_name);
}

void Skill::emitEvent(const QString &event, const QVariantMap &payload)
{
    SkillRegistry::instance().raise(m_name, event, payload);
}

} // namespace Kerfur
