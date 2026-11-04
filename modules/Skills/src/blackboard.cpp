// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "blackboard.h"

#include <QCoreApplication>
#include <QDir>
#include <QJSEngine>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(blackboardLog, "kerfur.skills.blackboard")

// Qt 6.11 added QQmlPropertyMap::create and deprecated the public constructor
// in its favour; older Qt only has the constructor.
QQmlPropertyMap *createValueMap(QObject *parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    return QQmlPropertyMap::create(parent);
#else
    return new QQmlPropertyMap{parent};
#endif
}

} // namespace

Blackboard::Blackboard(QObject *parent)
    : QObject{parent}, m_values{createValueMap(this)}, m_persistTimer{this}
{
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(persistDelayMs);
    connect(&m_persistTimer, &QTimer::timeout, this, &Blackboard::flush);

    // A write from QML -- `Blackboard.values.x = 3` -- has to look exactly like
    // a write through `set`, or a skill would have two behaviours depending on
    // which spelling its author reached for.
    connect(m_values, &QQmlPropertyMap::valueChanged, this, [this](const QString &key,
                                                                   const QVariant &value) {
        // Only the writes this class did not make itself. QQmlPropertyMap
        // reports assignments made through the meta-object -- which is what
        // `Blackboard.values.x = 3` in QML is -- and `set` below reports its
        // own, so without the guard a C++ write would be announced twice on
        // whichever Qt version happens to emit from `insert`.
        if (m_writing) {
            return;
        }
        emit changed(key, value);
        schedulePersist();
    });

    // The debounce means a value written seconds before the application closes
    // would never reach the disk. Connected here rather than relying on the
    // destructor, because this instance is a function local static and the
    // order it is torn down in relative to anything else is not something to
    // depend on.
    if (QCoreApplication *application{QCoreApplication::instance()}) {
        connect(application, &QCoreApplication::aboutToQuit, this, &Blackboard::flush);
    }

    load();
}

Blackboard &Blackboard::instance()
{
    static Blackboard blackboard;
    return blackboard;
}

Blackboard *Blackboard::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    // Not owned by the engine. The instance outlives any one engine and C++
    // holds a reference to it, so ownership is declared as C++'s.
    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

QString Blackboard::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/blackboard.json");
}

QStringList Blackboard::keys() const
{
    QStringList names{m_values->keys()};
    names.sort();
    return names;
}

void Blackboard::declare(const QString &key, const QVariant &defaultValue, bool persistent)
{
    if (key.isEmpty()) {
        return;
    }

    const bool isNew{!m_declarations.contains(key)};
    m_declarations.insert(key, Declaration{defaultValue, persistent});

    // A value restored from disk wins over the declared default. The whole
    // point of persistence is that yesterday's count is not lost because the
    // skill declaring it says the day starts at zero.
    if (!m_values->contains(key)) {
        m_writing = true;
        m_values->insert(key, defaultValue);
        m_writing = false;
        emit changed(key, defaultValue);
        schedulePersist();
    }
    if (isNew) {
        emit keysChanged();
    }
}

QVariant Blackboard::value(const QString &key, const QVariant &fallback) const
{
    const QVariant stored{m_values->value(key)};
    return stored.isValid() ? stored : fallback;
}

double Blackboard::number(const QString &key, double fallback) const
{
    bool ok{false};
    const double parsed{value(key).toDouble(&ok)};
    return ok ? parsed : fallback;
}

QString Blackboard::text(const QString &key, const QString &fallback) const
{
    const QVariant stored{m_values->value(key)};
    return stored.isValid() ? stored.toString() : fallback;
}

bool Blackboard::flag(const QString &key, bool fallback) const
{
    const QVariant stored{m_values->value(key)};
    return stored.isValid() ? stored.toBool() : fallback;
}

void Blackboard::set(const QString &key, const QVariant &value)
{
    if (key.isEmpty()) {
        return;
    }
    const bool isNew{!m_values->contains(key)};
    if (!isNew && m_values->value(key) == value) {
        return;
    }

    m_writing = true;
    m_values->insert(key, value);
    m_writing = false;

    emit changed(key, value);
    schedulePersist();
    if (isNew) {
        emit keysChanged();
    }
}

double Blackboard::add(const QString &key, double delta)
{
    const double total{number(key, 0.0) + delta};
    set(key, total);
    return total;
}

void Blackboard::reset(const QString &key)
{
    const auto declaration{m_declarations.constFind(key)};
    if (declaration == m_declarations.constEnd()) {
        if (m_values->contains(key)) {
            m_values->clear(key);
            emit changed(key, QVariant{});
            emit keysChanged();
            schedulePersist();
        }
        return;
    }
    set(key, declaration->defaultValue);
}

void Blackboard::clear()
{
    const QStringList existing{m_values->keys()};
    for (const QString &key : existing) {
        m_values->clear(key);
        emit changed(key, QVariant{});
    }
    m_writing = true;
    for (auto entry{m_declarations.constBegin()}; entry != m_declarations.constEnd(); ++entry) {
        m_values->insert(entry.key(), entry->defaultValue);
    }
    m_writing = false;
    emit keysChanged();
    schedulePersist();
}

QString Blackboard::expand(const QString &tmpl, const QVariantMap &extra) const
{
    if (!tmpl.contains(QLatin1Char{'{'}) && !tmpl.contains(QLatin1Char{'<'})) {
        return tmpl;
    }

    static const QRegularExpression placeholder{
        QStringLiteral("\\{([A-Za-z_][A-Za-z0-9_]*)\\}|<([A-Za-z_][A-Za-z0-9_]*)>")};

    QString expanded;
    qsizetype cursor{0};
    QRegularExpressionMatchIterator matches{placeholder.globalMatch(tmpl)};
    while (matches.hasNext()) {
        const QRegularExpressionMatch match{matches.next()};
        expanded += QStringView{tmpl}.mid(cursor, match.capturedStart() - cursor);

        const QString key{match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)};
        // The caller's own values first: the slots of the request being
        // confirmed are what "set X to Y" has to read Y from, and they are not
        // on the blackboard yet precisely because they have not been confirmed.
        const QVariant supplied{extra.value(key)};
        const QVariant resolved{supplied.isValid() ? supplied : m_values->value(key)};
        expanded += resolved.isValid() ? resolved.toString() : match.captured(0);

        cursor = match.capturedEnd();
    }
    expanded += QStringView{tmpl}.mid(cursor);
    return expanded;
}

QVariantMap Blackboard::snapshot() const
{
    QVariantMap map;
    const QStringList names{m_values->keys()};
    for (const QString &key : names) {
        map.insert(key, m_values->value(key));
    }
    return map;
}

void Blackboard::schedulePersist()
{
    m_persistTimer.start();
}

void Blackboard::load()
{
    QFile file{storagePath()};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument document{QJsonDocument::fromJson(file.readAll())};
    if (!document.isObject()) {
        qCWarning(blackboardLog) << storagePath() << "is not an object; starting empty";
        return;
    }

    m_writing = true;
    const QJsonObject object{document.object()};
    for (auto entry{object.constBegin()}; entry != object.constEnd(); ++entry) {
        m_values->insert(entry.key(), entry.value().toVariant());
    }
    m_writing = false;
    qCInfo(blackboardLog) << "restored" << object.size() << "values from" << storagePath();
    emit keysChanged();
}

void Blackboard::flush()
{
    m_persistTimer.stop();

    QJsonObject object;
    const QStringList names{m_values->keys()};
    for (const QString &key : names) {
        // An undeclared key is persisted. It was set by somebody deliberately,
        // and forgetting it on restart because nobody wrote a declaration would
        // be a rule that only bites the skills written in a hurry.
        const auto declaration{m_declarations.constFind(key)};
        if (declaration != m_declarations.constEnd() && !declaration->persistent) {
            continue;
        }
        object.insert(key, QJsonValue::fromVariant(m_values->value(key)));
    }

    const QFileInfo info{storagePath()};
    if (!QDir{}.mkpath(info.absolutePath())) {
        qCWarning(blackboardLog) << "cannot create" << info.absolutePath();
        return;
    }

    QSaveFile file{storagePath()};
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(blackboardLog) << "cannot write" << storagePath();
        return;
    }
    file.write(QJsonDocument{object}.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(blackboardLog) << "cannot commit" << storagePath();
    }
}

} // namespace Kerfur
