// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "skillstore.h"

#include <QLoggingCategory>
#include <QStandardPaths>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(storeLog, "kerfur.skills.store")

} // namespace

} // namespace Kerfur

#ifdef Q_OS_WASM

// Mounted on IndexedDB and read back once. A write before that read completes
// would be overwritten by it, which is why nothing reads or writes the store
// before `opened`.
EM_JS(void, kerfurMountSkillStore, (const char *path), {
    const directory = UTF8ToString(path);
    try {
        FS.mkdirTree(directory);
        FS.mount(IDBFS, {}, directory);
    } catch (error) {
        console.warn("skills: cannot mount IndexedDB at " + directory + ": " + error);
        _kerfurSkillStoreLoaded(0);
        return;
    }
    FS.syncfs(true, function(error) {
        if (error) {
            console.warn("skills: cannot read IndexedDB: " + error);
        }
        _kerfurSkillStoreLoaded(error ? 0 : 1);
    });
})

EM_JS(void, kerfurPersistSkillStore, (), {
    FS.syncfs(false, function(error) {
        if (error) {
            console.warn("skills: cannot write IndexedDB: " + error);
        }
    });
})

EM_JS(void, kerfurRequestDurableSkillStore, (), {
    if (typeof navigator === "undefined" || !navigator.storage || !navigator.storage.persist) {
        return;
    }
    navigator.storage.persisted()
        .then(function(persisted) { return persisted || navigator.storage.persist(); })
        .catch(function(error) { console.warn("skills: cannot request persistent storage: " + error); });
})

extern "C" EMSCRIPTEN_KEEPALIVE void kerfurSkillStoreLoaded(int loaded)
{
    QMetaObject::invokeMethod(&Kerfur::SkillStore::instance(),
                              "applyLoaded",
                              Qt::QueuedConnection,
                              Q_ARG(bool, loaded != 0));
}

#endif

namespace Kerfur {

SkillStore::SkillStore(QObject *parent)
    : QObject{parent}
    , m_directory{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + QStringLiteral("/skill-store")}
{}

SkillStore &SkillStore::instance()
{
    static SkillStore store;
    return store;
}

QString SkillStore::skillsDirectory() const
{
    return m_directory + QStringLiteral("/skills");
}

void SkillStore::open()
{
    if (m_open || m_opening) {
        return;
    }
    m_opening = true;
#ifdef Q_OS_WASM
    kerfurMountSkillStore(m_directory.toUtf8().constData());
#else
    QMetaObject::invokeMethod(this, "applyLoaded", Qt::QueuedConnection, Q_ARG(bool, true));
#endif
}

void SkillStore::applyLoaded(bool loaded)
{
    m_open = true;
    m_opening = false;
    if (loaded) {
        qCInfo(storeLog) << "skill store at" << m_directory;
    } else {
        qCWarning(storeLog) << "skill store at" << m_directory << "lasts this session only";
    }
    emit opened(loaded);
}

void SkillStore::persist()
{
#ifdef Q_OS_WASM
    if (m_open) {
        kerfurPersistSkillStore();
    }
#endif
}

void SkillStore::requestDurable()
{
#ifdef Q_OS_WASM
    kerfurRequestDurableSkillStore();
#endif
}

} // namespace Kerfur
