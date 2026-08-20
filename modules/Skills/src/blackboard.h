// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_BLACKBOARD_H
#define KERFUR_BLACKBOARD_H

#include <QHash>
#include <QObject>
#include <QQmlPropertyMap>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Every named value the skills share, in one place, and the only place.
///
/// A skill is a set of reactions to things being said; the state those
/// reactions read and write has to outlive any one of them, be visible to all
/// of them, and survive a restart. Putting it in the skill that happens to have
/// created it would mean a second skill could not see it, and putting it in C++
/// would mean adding a member every time somebody counts something new.
///
/// The values are exposed through a QQmlPropertyMap, so QML binds to them by
/// name:
///
///     readonly property real calories: Blackboard.values.caloriesOfTheDay
///     onCaloriesChanged: if (calories > limit) ...
///
/// which is the whole reason for the map: a plain `QVariantMap` property would
/// notify on every write to anything and could not be bound to per key.
class Blackboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Bind through this. Reading `Blackboard.values.x` for an undeclared `x`
    /// gives undefined rather than an error, which is what makes a skill that
    /// declares its variables and one that does not both work.
    Q_PROPERTY(QQmlPropertyMap *values READ values CONSTANT)

    /// Every declared key, sorted. For the panel.
    Q_PROPERTY(QStringList keys READ keys NOTIFY keysChanged)

    /// Everything, as one map, renotified on every change.
    ///
    /// `values` is the property to bind a single named variable to; this is for
    /// a view that lists all of them and cannot name any of them at the time it
    /// is written. Binding to `values[someKey]` with a computed key would have
    /// to depend on a property whose name is not known until run time, and this
    /// is the honest way to express that dependency.
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)

    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)

public:
    explicit Blackboard(QObject *parent = nullptr);

    /// The one instance, reachable from C++ as well as from QML, for the same
    /// reason WorldModel has one: the QML singleton machinery alone would give
    /// the engine a second instance that no C++ caller could find.
    static Blackboard &instance();
    static Blackboard *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QQmlPropertyMap *values() const { return m_values; }
    QStringList keys() const;
    QString storagePath() const;

    /// Registers a key, its starting value and whether it survives a restart.
    ///
    /// Declaring is optional -- `set` on an unknown key works -- but a declared
    /// key has a default to be reset to and a persistence decision that was
    /// made deliberately rather than by accident.
    Q_INVOKABLE void declare(const QString &key,
                             const QVariant &defaultValue,
                             bool persistent = true);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE double number(const QString &key, double fallback = 0.0) const;
    Q_INVOKABLE QString text(const QString &key, const QString &fallback = {}) const;
    Q_INVOKABLE bool flag(const QString &key, bool fallback = false) const;

    Q_INVOKABLE void set(const QString &key, const QVariant &value);

    /// Adds to a number and returns the total. The single most common thing a
    /// skill does, and writing it as read-modify-write in QML is where the race
    /// with a background routine would live.
    Q_INVOKABLE double add(const QString &key, double delta);

    /// Back to the declared default. An undeclared key is removed instead,
    /// because it has no default to go back to.
    Q_INVOKABLE void reset(const QString &key);

    Q_INVOKABLE void clear();

    /// Substitutes `{key}` and `<key>` with the current values.
    ///
    /// Both spellings, because `<amount>` is how a person writes a placeholder
    /// in a sentence they are going to say out loud and `{amount}` is how they
    /// write one in a pattern, and being made to remember which is which at the
    /// moment of writing "You are at <caloriesOfTheDay>" is friction for
    /// nothing.
    Q_INVOKABLE QString expand(const QString &tmpl, const QVariantMap &extra = {}) const;

    /// Everything, as one map. What the language backend is handed, and what
    /// the panel lists.
    QVariantMap snapshot() const;

    /// Writes now rather than on the debounce. Called at shutdown.
    void flush();

signals:
    /// One key changed. Carries the key so that a `Watch` can filter, and the
    /// value so that it does not have to read back.
    void changed(const QString &key, const QVariant &value);
    void keysChanged();

private:
    void load();
    void schedulePersist();

    struct Declaration
    {
        QVariant defaultValue;
        bool persistent{true};
    };

    /// Created rather than held by value: Qt 6.11 deprecated the public
    /// one argument constructor in favour of the factory, and the factory
    /// returns a pointer.
    QQmlPropertyMap *m_values{nullptr};
    QHash<QString, Declaration> m_declarations;
    QTimer m_persistTimer;

    /// True while this class is writing into the map, so that the map's own
    /// change signal can tell a QML assignment from an echo of `set`.
    bool m_writing{false};

    /// How long writes are coalesced before hitting the disk. A skill that
    /// counts something can write on every utterance, and a robot that rewrites
    /// a file on an SD card each time somebody speaks wears the card out.
    static constexpr int persistDelayMs{1500};
};

} // namespace Kerfur

#endif // KERFUR_BLACKBOARD_H
