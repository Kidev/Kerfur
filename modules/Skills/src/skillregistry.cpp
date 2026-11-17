// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "skillregistry.h"

#include "assetsearch.h"
#include "blackboard.h"
#include "session.h"
#include "skill.h"
#include "skillsource.h"
#include "skillstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>

#include <algorithm>
#include <type_traits>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(registryLog, "kerfur.skills.registry")

constexpr QLatin1StringView settingsKey{"Paths/skillsPath"};
constexpr QLatin1StringView folderName{"skills"};

} // namespace

SkillRegistry::SkillRegistry(QObject *parent) : QObject{parent}, m_confirmationTimer{this}
{
    qRegisterMetaType<Kerfur::LanguageResolution>("Kerfur::LanguageResolution");

    m_confirmationTimer.setSingleShot(true);
    m_confirmationTimer.setInterval(confirmationTimeoutMs);
    connect(&m_confirmationTimer, &QTimer::timeout, this, [this]() {
        if (m_confirmation.active) {
            note(QStringLiteral("question timed out: %1").arg(m_confirmation.question));
            clearConfirmation();
        }
    });

    setBackend(new PatternLanguageBackend{this});
}

SkillRegistry::~SkillRegistry() = default;

static_assert(!std::is_default_constructible_v<SkillRegistry>,
              "QML would construct its own SkillRegistry instead of calling create()");

SkillRegistry &SkillRegistry::instance()
{
    static SkillRegistry registry{nullptr};
    return registry;
}

SkillRegistry *SkillRegistry::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(scriptEngine)
    SkillRegistry &registry{instance()};
    QJSEngine::setObjectOwnership(&registry, QJSEngine::CppOwnership);

    registry.m_engine = engine;
    // A light session runs no skill: it hears nothing to trigger one, and the
    // panel shows none to edit.
    if (Session::light()) {
        return &registry;
    }
    // The skills load once the store has been read: it holds the skills added
    // from the panel and, on the web build, the blackboard they read. `opened`
    // is queued, so loading skill files -- which instantiate QML components
    // and therefore re-enter the engine -- happens after the engine has
    // finished creating this singleton rather than inside that call.
    SkillStore &store{SkillStore::instance()};
    connect(&store,
            &SkillStore::opened,
            &registry,
            &SkillRegistry::applyStoreOpened,
            Qt::UniqueConnection);
    store.open();
    return &registry;
}

void SkillRegistry::applyStoreOpened(bool loaded)
{
    if (!loaded) {
        note(QStringLiteral("browser storage unavailable; added skills last this session only"));
    }
    m_storeReady = true;
    Blackboard::instance().restore();
    reload();
}

QString SkillRegistry::storeDirectory() const
{
    return SkillStore::instance().skillsDirectory();
}

void SkillRegistry::setBackend(LanguageBackend *backend)
{
    if (backend == nullptr || backend == m_backend) {
        return;
    }
    if (m_backend != nullptr) {
        m_backend->disconnect(this);
        m_backend->deleteLater();
    }
    m_backend = backend;
    m_backend->setParent(this);
    connect(m_backend, &LanguageBackend::resolved, this, &SkillRegistry::applyResolution);
    emit backendChanged();
}

QString SkillRegistry::backendName() const
{
    return m_backend != nullptr ? m_backend->name() : QStringLiteral("none");
}

QStringList SkillRegistry::skillNames() const
{
    QStringList names;
    for (const QPointer<Skill> &skill : m_skills) {
        if (!skill.isNull()) {
            names.append(skill->name());
        }
    }
    names.sort();
    return names;
}

QStringList SkillRegistry::catalogue() const
{
    QStringList entries;
    for (const QPointer<Skill> &skill : m_skills) {
        if (skill.isNull()) {
            continue;
        }
        for (const Trigger *trigger : skill->triggers()) {
            entries.append(QStringLiteral("%1: %2").arg(skill->name(), trigger->intent()));
        }
    }
    entries.sort();
    return entries;
}

void SkillRegistry::loadSkills()
{
    m_files.clear();
    if (m_engine == nullptr) {
        m_problem = QStringLiteral("no QML engine; skills not loaded");
        emit skillsChanged();
        return;
    }

    QStringList problems;
    m_directory.clear();

    for (const QString &candidate : AssetSearch::candidates(QString{settingsKey},
                                                            QString{folderName})) {
        const QDir directory{candidate};
        if (directory.exists()
            && !directory.entryInfoList({QStringLiteral("*.qml")}, QDir::Files | QDir::Readable)
                    .isEmpty()) {
            m_directory = directory.absolutePath();
            break;
        }
    }

    // The bundled skills, then the ones added from the panel. An added file
    // with the name of a bundled one replaces it, which is how a bundled skill
    // is edited where the bundled directory cannot be written.
    struct SkillFile
    {
        QFileInfo info;
        bool added{false};
        bool overrides{false};
    };
    QMap<QString, SkillFile> files;
    const auto collect = [&files](const QString &path, bool added) {
        if (path.isEmpty()) {
            return;
        }
        const QDir directory{path};
        const QFileInfoList found{directory.entryInfoList({QStringLiteral("*.qml")},
                                                          QDir::Files | QDir::Readable,
                                                          QDir::Name)};
        for (const QFileInfo &info : found) {
            const bool overrides{added && files.contains(info.fileName())};
            files.insert(info.fileName(), SkillFile{info, added, overrides});
        }
    };
    collect(m_directory, false);
    if (m_storeReady) {
        collect(SkillStore::instance().skillsDirectory(), true);
    }

    if (files.isEmpty()) {
        m_problem = QStringLiteral("no skills directory; searched %1")
                        .arg(AssetSearch::describe(QString{settingsKey}, QString{folderName}));
        qCInfo(registryLog) << m_problem;
        emit skillsChanged();
        return;
    }

    for (const SkillFile &file : std::as_const(files)) {
        QVariantMap entry{
            {QStringLiteral("name"), file.info.completeBaseName()},
            {QStringLiteral("description"), QString{}},
            {QStringLiteral("file"), file.info.fileName()},
            {QStringLiteral("path"), file.info.absoluteFilePath()},
            {QStringLiteral("added"), file.added},
            {QStringLiteral("overrides"), file.overrides},
            {QStringLiteral("loaded"), false},
        };

        QQmlComponent component{m_engine, QUrl::fromLocalFile(file.info.absoluteFilePath())};
        if (component.isError()) {
            // Named, not swallowed. A skill file is content written by hand and
            // a typo in one must not take the others down with it, but it must
            // be impossible to miss. It stays listed, so that an added skill
            // that no longer compiles can still be edited or removed.
            const QString failure{QStringLiteral("%1: %2").arg(file.info.fileName(),
                                                               component.errorString().trimmed())};
            problems.append(failure);
            qCWarning(registryLog).noquote() << failure;
            m_files.append(entry);
            continue;
        }

        // Its own context, so that a skill file's ids and properties cannot
        // collide with another's.
        auto *context{new QQmlContext(m_engine->rootContext(), this)};
        QObject *root{component.create(context)};
        if (root == nullptr) {
            problems.append(QStringLiteral("%1: produced nothing").arg(file.info.fileName()));
            delete context;
            m_files.append(entry);
            continue;
        }
        root->setParent(this);
        context->setParent(root);
        m_roots.append(root);

        const Skill *skill{qobject_cast<Skill *>(root)};
        if (skill == nullptr) {
            problems.append(QStringLiteral("%1: root is not a Skill").arg(file.info.fileName()));
        } else {
            entry.insert(QStringLiteral("name"), skill->name());
            entry.insert(QStringLiteral("description"), skill->description());
            entry.insert(QStringLiteral("loaded"), true);
        }
        m_files.append(entry);
    }

    m_problem = problems.join(QStringLiteral("; "));
    qCInfo(registryLog) << "loaded" << m_skills.size() << "skills from" << m_directory
                        << SkillStore::instance().skillsDirectory();
    emit skillsChanged();
}

void SkillRegistry::reload()
{
    for (QObject *root : std::as_const(m_roots)) {
        delete root;
    }
    m_roots.clear();
    m_skills.clear();
    m_inFlight.clear();
    clearConfirmation();
    // The engine keeps every compiled file by URL. A file that was edited is
    // read again only once its old compilation, unused now, has been dropped.
    if (m_engine != nullptr) {
        m_engine->trimComponentCache();
    }
    loadSkills();
}

QString SkillRegistry::sourceOf(const QString &path) const
{
    const bool listed{std::any_of(m_files.cbegin(), m_files.cend(), [&path](const QVariant &file) {
        return file.toMap().value(QStringLiteral("path")).toString() == path;
    })};
    if (!listed) {
        return {};
    }
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.read(maximumSkillBytes));
}

QString SkillRegistry::install(const QString &source, const QString &replacing)
{
    SkillStore &store{SkillStore::instance()};
    if (m_engine == nullptr || !m_storeReady) {
        return QStringLiteral("The skill store is not open yet.");
    }
    const QByteArray bytes{source.toUtf8()};
    if (bytes.size() > maximumSkillBytes) {
        return QStringLiteral("A skill is limited to %1 KiB.").arg(maximumSkillBytes / 1024);
    }
    const QString fileName{skillFileName(declaredSkillName(source))};
    if (fileName.isEmpty()) {
        return QStringLiteral("Give the skill a name, such as name: \"greeting\".");
    }

    const QDir directory{store.skillsDirectory()};
    if (!directory.mkpath(QStringLiteral("."))) {
        return QStringLiteral("Cannot create %1.").arg(directory.path());
    }
    const QString path{directory.filePath(fileName)};

    // Compiled against the URL it will be loaded from, before anything is
    // written: a skill that does not compile never replaces one that does.
    QQmlComponent component{m_engine};
    component.setData(bytes, QUrl::fromLocalFile(path));
    if (component.isError()) {
        return component.errorString().trimmed();
    }
    if (!component.isReady()) {
        return QStringLiteral("The skill imports something that is not available offline.");
    }

    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return QStringLiteral("Cannot write %1.").arg(path);
    }

    const QString previous{QFileInfo{replacing}.fileName()};
    if (!previous.isEmpty() && previous != fileName
        && previous.endsWith(QStringLiteral(".qml"))) {
        QFile::remove(directory.filePath(previous));
    }

    store.persist();
    store.requestDurable();
    note(QStringLiteral("saved skill %1").arg(fileName));
    reload();
    return {};
}

QString SkillRegistry::uninstall(const QString &file)
{
    const QString fileName{QFileInfo{file}.fileName()};
    const QDir directory{SkillStore::instance().skillsDirectory()};
    if (!m_storeReady || fileName.isEmpty() || !fileName.endsWith(QStringLiteral(".qml"))
        || !directory.exists(fileName)) {
        return QStringLiteral("No added skill is called %1.").arg(file);
    }
    if (!QFile::remove(directory.filePath(fileName))) {
        return QStringLiteral("Cannot delete %1.").arg(directory.filePath(fileName));
    }
    SkillStore::instance().persist();
    note(QStringLiteral("removed skill %1").arg(fileName));
    reload();
    return {};
}

void SkillRegistry::registerSkill(Skill *skill)
{
    if (skill == nullptr || m_skills.contains(skill)) {
        return;
    }
    m_skills.append(skill);

    for (Trigger *trigger : skill->triggers()) {
        connect(trigger, &QObject::destroyed, this, &SkillRegistry::skillsChanged);
    }
    qCInfo(registryLog) << "skill" << skill->name() << "offers" << skill->intents();
    emit skillsChanged();
}

Trigger *SkillRegistry::findTrigger(const QString &key) const
{
    for (const QPointer<Skill> &skill : m_skills) {
        if (skill.isNull()) {
            continue;
        }
        for (Trigger *trigger : skill->triggers()) {
            if (trigger->key() == key) {
                return trigger;
            }
        }
    }
    return nullptr;
}

void SkillRegistry::hear(const QString &text,
                         const QString &speaker,
                         int priority,
                         bool authorised)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    m_lastUtterance = text;
    m_lastSpeaker = speaker;
    m_lastAuthorised = authorised;
    m_lastIntent.clear();
    emit heardChanged();

    // An outstanding question first. It is answered by the person who was
    // asked and by nobody else, so a second voice saying "yes" across the room
    // is just another sentence.
    if (m_confirmation.active
        && (m_confirmation.speaker.isEmpty() || m_confirmation.speaker == speaker)) {
        if (isAffirmative(text)) {
            const Confirmation confirmed{m_confirmation};
            clearConfirmation();
            note(QStringLiteral("confirmed: %1").arg(confirmed.question));
            if (!confirmed.trigger.isNull()) {
                fire(confirmed.trigger, confirmed.captures, speaker);
            } else if (confirmed.callback.isCallable()) {
                QJSValue callback{confirmed.callback};
                const QJSValue result{callback.call()};
                if (result.isError()) {
                    qCWarning(registryLog) << "confirmation handler threw" << result.toString();
                }
            }
            return;
        }
        if (isNegative(text)) {
            note(QStringLiteral("cancelled: %1").arg(m_confirmation.question));
            clearConfirmation();
            return;
        }
        // Neither. The question stands until it times out, and this sentence is
        // treated as what it looks like: a new request.
    }

    QList<IntentSpec> intents;
    for (const QPointer<Skill> &skill : m_skills) {
        if (skill.isNull() || !skill->isEnabled()) {
            continue;
        }
        for (const Trigger *trigger : skill->triggers()) {
            if (!trigger->isEnabled()) {
                continue;
            }
            intents.append(IntentSpec{trigger->key(),
                                      trigger->intent(),
                                      trigger->phrases(),
                                      skill->name()});
        }
    }

    if (intents.isEmpty() || m_backend == nullptr) {
        emit unmatched(text, speaker);
        return;
    }

    LanguageRequest request;
    request.utterance = text;
    request.speaker = speaker;
    request.priority = priority;
    request.intents = intents;
    request.variables = Blackboard::instance().snapshot();
    request.token = ++m_token;

    m_inFlight.insert(request.token, Utterance{text, speaker, priority, authorised});
    m_backend->resolve(request);
}

void SkillRegistry::applyResolution(const Kerfur::LanguageResolution &resolution)
{
    const auto found{m_inFlight.constFind(resolution.token)};
    if (found == m_inFlight.constEnd()) {
        // The request was cancelled, or the backend answered twice. Either way
        // there is nothing left to apply it to.
        return;
    }
    const Utterance utterance{*found};
    m_inFlight.remove(resolution.token);

    if (resolution.intent.id.isEmpty()) {
        emit unmatched(utterance.text, utterance.speaker);
        return;
    }

    Trigger *trigger{findTrigger(resolution.intent.id)};
    if (trigger == nullptr || !trigger->isEnabled()) {
        // The skill was reloaded between the question and the answer.
        emit unmatched(utterance.text, utterance.speaker);
        return;
    }

    m_lastIntent = trigger->intent();
    emit heardChanged();

    // The trigger's own rule decides. `authorised` from the recogniser is the
    // global default and is reported alongside, but a trigger that opened
    // itself to everybody means it.
    if (utterance.priority > trigger->authority()) {
        note(QStringLiteral("refused %1 from %2 (rank %3)")
                 .arg(trigger->intent(),
                      utterance.speaker.isEmpty() ? QStringLiteral("a stranger")
                                                  : utterance.speaker)
                 .arg(utterance.priority));
        emit trigger->refused(utterance.speaker, utterance.priority);
        emit commandRefused(trigger->intent(), utterance.speaker, utterance.priority);
        return;
    }

    if (!trigger->confirm().isEmpty()) {
        m_confirmation = Confirmation{};
        m_confirmation.active = true;
        m_confirmation.question = Blackboard::instance().expand(trigger->confirm(),
                                                                resolution.intent.captures);
        m_confirmation.speaker = utterance.speaker;
        m_confirmation.from = trigger->skill() != nullptr ? trigger->skill()->name() : QString{};
        m_confirmation.trigger = trigger;
        m_confirmation.captures = resolution.intent.captures;
        m_confirmationTimer.start();
        emit confirmationChanged();

        say(m_confirmation.question, m_confirmation.from);
        return;
    }

    fire(trigger, resolution.intent.captures, utterance.speaker);
}

void SkillRegistry::fire(Trigger *trigger, const QVariantMap &captures, const QString &speaker)
{
    if (trigger == nullptr) {
        return;
    }
    note(QStringLiteral("%1 -> %2").arg(speaker.isEmpty() ? QStringLiteral("someone") : speaker,
                                        trigger->intent()));
    emit trigger->fired(captures, speaker);
}

void SkillRegistry::requestConfirmation(const QString &question,
                                        const QJSValue &onConfirmed,
                                        const QString &speaker,
                                        const QString &from)
{
    m_confirmation = Confirmation{};
    m_confirmation.active = true;
    m_confirmation.question = question;
    m_confirmation.speaker = speaker;
    m_confirmation.from = from;
    m_confirmation.callback = onConfirmed;
    m_confirmationTimer.start();
    emit confirmationChanged();

    say(question, from);
}

void SkillRegistry::confirmPending()
{
    if (!m_confirmation.active) {
        return;
    }
    hear(QStringLiteral("yes"), m_confirmation.speaker, 0, true);
}

void SkillRegistry::cancelPending()
{
    if (!m_confirmation.active) {
        return;
    }
    note(QStringLiteral("cancelled: %1").arg(m_confirmation.question));
    clearConfirmation();
}

void SkillRegistry::clearConfirmation()
{
    m_confirmationTimer.stop();
    if (!m_confirmation.active) {
        return;
    }
    m_confirmation = Confirmation{};
    emit confirmationChanged();
}

void SkillRegistry::say(const QString &text, const QString &from)
{
    if (text.isEmpty()) {
        return;
    }
    setLastSpoken(text);
    note(QStringLiteral("says: %1").arg(text));
    emit spoke(text, from);
}

void SkillRegistry::setLastSpoken(const QString &text)
{
    m_lastSpoken = text;
    // Unconditionally, for the same reason the transcript list is: saying the
    // same sentence twice is two events, and a view that compared them would
    // show the first one for ever.
    emit spokenChanged();
}

void SkillRegistry::raise(const QString &skill, const QString &event, const QVariantMap &payload)
{
    note(QStringLiteral("%1 raised %2").arg(skill, event));
    emit eventRaised(skill, event, payload);
}

void SkillRegistry::note(const QString &line)
{
    qCInfo(registryLog).noquote() << line;
    m_activity.prepend(line);
    while (m_activity.size() > activityHistory) {
        m_activity.removeLast();
    }
    emit activityChanged();
}

} // namespace Kerfur
