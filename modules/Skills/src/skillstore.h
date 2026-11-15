// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SKILLSTORE_H
#define KERFUR_SKILLSTORE_H

#include <QObject>
#include <QString>

namespace Kerfur {

/// The writable half of the skills: `skill-store` under the application data
/// location, holding the skills added from the panel (`skills/`) and, on the
/// web build, the blackboard.
///
/// The bundled skills directory is read only in an install tree and in the
/// browser, so anything written at run time lands here instead. On the web
/// build the directory is an IndexedDB mount: `open` reads it back
/// asynchronously, nothing in it is valid until `opened` has been emitted, and
/// `persist` writes it back after every change.
class SkillStore : public QObject
{
    Q_OBJECT

public:
    static SkillStore &instance();

    /// The store's root, created on the first write.
    QString directory() const { return m_directory; }

    /// Where the skills added from the panel are kept, one `.qml` each.
    QString skillsDirectory() const;

    bool isOpen() const { return m_open; }

    /// Mounts and reads the store, then emits `opened`, queued, on every
    /// platform. Later calls do nothing; check `isOpen` first.
    void open();

    /// Flushes the store to IndexedDB on the web build. Nothing elsewhere.
    void persist();

    /// Asks the browser not to clear the store under disk pressure. Called once
    /// there is something in it worth keeping. Nothing outside the web build.
    void requestDurable();

signals:
    /// The store can be read. `loaded` is false when the browser storage could
    /// not be mounted: the directory then works for this session only.
    void opened(bool loaded);

private slots:
    void applyLoaded(bool loaded);

private:
    explicit SkillStore(QObject *parent = nullptr);

    QString m_directory;
    bool m_open{false};
    bool m_opening{false};
};

} // namespace Kerfur

#endif // KERFUR_SKILLSTORE_H
