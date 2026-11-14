// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SKILLREGISTRY_H
#define KERFUR_SKILLREGISTRY_H

#include "intent.h"
#include "languagebackend.h"

#include <QDateTime>
#include <QHash>
#include <QJSValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

class Skill;
class Trigger;

/// Everything the loaded skills can do, and the one path from a sentence to
/// one of them.
///
/// Kerfur listens to everybody and obeys one person, and this is where that
/// happens. Every utterance is matched, logged and shown in the panel whoever
/// said it; only an utterance from somebody at or above a trigger's own
/// `authority` is allowed to fire it. Refusal is a signal rather than silence,
/// because a command that was heard and declined and one that was not heard at
/// all are different problems and look identical from the outside.
class SkillRegistry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Skills)
    QML_SINGLETON

    /// The names of the loaded skills.
    Q_PROPERTY(QStringList skillNames READ skillNames NOTIFY skillsChanged)

    /// Every trigger, as `skill: intent`. For the panel, and for anybody
    /// wondering what Kerfur will answer to.
    Q_PROPERTY(QStringList catalogue READ catalogue NOTIFY skillsChanged)

    /// Where the `.qml` files were read from, and what went wrong if anything
    /// did. A skill that failed to compile is named here rather than only in
    /// the log, because a skill file is content and a content error should be
    /// visible in the interface that shows content.
    Q_PROPERTY(QString directory READ directory NOTIFY skillsChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY skillsChanged)

    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)

    /// The last thing heard, what it was understood as, and who said it.
    Q_PROPERTY(QString lastUtterance READ lastUtterance NOTIFY heardChanged)
    Q_PROPERTY(QString lastIntent READ lastIntent NOTIFY heardChanged)
    Q_PROPERTY(QString lastSpeaker READ lastSpeaker NOTIFY heardChanged)
    Q_PROPERTY(bool lastAuthorised READ lastAuthorised NOTIFY heardChanged)

    /// The last thing Kerfur said, or would have said if it could speak yet.
    Q_PROPERTY(QString lastSpoken READ lastSpoken NOTIFY spokenChanged)

    /// A question is outstanding and the next thing its asker says will be read
    /// as the answer.
    Q_PROPERTY(bool awaitingConfirmation READ isAwaitingConfirmation NOTIFY confirmationChanged)
    Q_PROPERTY(QString question READ question NOTIFY confirmationChanged)

    /// The last few things that happened, newest first. One line each.
    Q_PROPERTY(QStringList activity READ activity NOTIFY activityChanged)

public:
    /// No default argument. The QML engine builds a default constructible
    /// singleton itself and never calls `create`, so QML would hold a second
    /// instance that C++ cannot see.
    explicit SkillRegistry(QObject *parent);
    ~SkillRegistry() override;

    static SkillRegistry &instance();
    static SkillRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QStringList skillNames() const;
    QStringList catalogue() const;
    QString directory() const { return m_directory; }
    QString problem() const { return m_problem; }
    QString backendName() const;
    QString lastUtterance() const { return m_lastUtterance; }
    QString lastIntent() const { return m_lastIntent; }
    QString lastSpeaker() const { return m_lastSpeaker; }
    bool lastAuthorised() const { return m_lastAuthorised; }
    QString lastSpoken() const { return m_lastSpoken; }
    bool isAwaitingConfirmation() const { return m_confirmation.active; }
    QString question() const { return m_confirmation.question; }
    QStringList activity() const { return m_activity; }

    /// Replaces what understands language. Ownership is taken.
    ///
    /// This is the whole extension point for a language model: construct one,
    /// hand it over, and every skill already written keeps working unchanged.
    void setBackend(LanguageBackend *backend);

    /// Called by a Skill when its component completes.
    void registerSkill(Skill *skill);

    /// Called by Skill::say.
    void say(const QString &text, const QString &from);

    /// Called by Skill::emitEvent.
    void raise(const QString &skill, const QString &event, const QVariantMap &payload);

    /// Called by Skill::ask and by a Trigger carrying a `confirm`.
    void requestConfirmation(const QString &question,
                             const QJSValue &onConfirmed,
                             const QString &speaker,
                             const QString &from);

public slots:
    /// One complete utterance. This is what SpeechRecognizer's `utteranceReady`
    /// is connected to.
    ///
    /// `authorised` is the recogniser's own reading of the speaker's rank
    /// against the global ceiling; a trigger may still be stricter or looser
    /// through its own `authority`, and the trigger's rule is the one that
    /// decides. This is passed in anyway so that the panel can show that the
    /// two agree, and so that a build with no ranking at all still refuses
    /// nothing rather than everything.
    void hear(const QString &text, const QString &speaker, int priority, bool authorised);

    /// Answers an outstanding question from the interface rather than by voice.
    void confirmPending();
    void cancelPending();

    /// Re-reads the skills directory. For development: edit a `.qml`, press the
    /// button, no rebuild and no restart.
    void reload();

signals:
    void skillsChanged();
    void backendChanged();
    void heardChanged();
    void spokenChanged();
    void confirmationChanged();
    void activityChanged();

    /// Kerfur wants to say something. Nothing speaks yet; when something does,
    /// it connects here and nothing else changes.
    void spoke(const QString &text, const QString &from);

    /// A skill raised a named event. The bridge to anything outside Kerfur --
    /// house automation, a light, a message -- listens to this, so that no
    /// skill file can reach the outside world directly.
    void eventRaised(const QString &skill, const QString &event, const QVariantMap &payload);

    /// Somebody who is not allowed to gave an order that was otherwise
    /// understood.
    void commandRefused(const QString &intent, const QString &speaker, int priority);

    /// An utterance nobody claimed. Useful for a language model backend that
    /// wants to answer it anyway.
    void unmatched(const QString &text, const QString &speaker);

private:
    void loadSkills();
    void applyResolution(const Kerfur::LanguageResolution &resolution);
    void fire(Trigger *trigger, const QVariantMap &captures, const QString &speaker);
    void note(const QString &line);
    void setLastSpoken(const QString &text);
    void clearConfirmation();
    Trigger *findTrigger(const QString &key) const;

    /// What `hear` knew when it asked, kept until the backend answers.
    struct Utterance
    {
        QString text;
        QString speaker;
        int priority{0};
        bool authorised{false};
    };

    struct Confirmation
    {
        bool active{false};
        QString question;
        QString speaker;
        QString from;
        QJSValue callback;
        QPointer<Trigger> trigger;
        QVariantMap captures;
    };

    QQmlEngine *m_engine{nullptr};
    LanguageBackend *m_backend{nullptr};

    QList<QPointer<Skill>> m_skills;
    QList<QObject *> m_roots;

    QHash<quint64, Utterance> m_inFlight;
    quint64 m_token{0};

    Confirmation m_confirmation;
    QTimer m_confirmationTimer;

    QString m_directory;
    QString m_problem;
    QString m_lastUtterance;
    QString m_lastIntent;
    QString m_lastSpeaker;
    QString m_lastSpoken;
    bool m_lastAuthorised{false};

    QStringList m_activity;

    /// How long an unanswered question stands. Long enough to think about it,
    /// short enough that a "yes" to something else half a minute later cannot
    /// be mistaken for an answer.
    static constexpr int confirmationTimeoutMs{20'000};

    static constexpr qsizetype activityHistory{12};
};

} // namespace Kerfur

#endif // KERFUR_SKILLREGISTRY_H
