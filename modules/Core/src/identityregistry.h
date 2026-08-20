// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_IDENTITYREGISTRY_H
#define KERFUR_IDENTITYREGISTRY_H

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kerfur {

/// One person Kerfur is allowed to know, as `identities/ids.json` declares
/// them.
///
/// The face and the voice are two files that have to be understood as one
/// person, and before this file existed the only thing joining them was that
/// they shared a stem. That is a rule nobody can see, it forbids a person whose
/// display name is not a legal file name, and it has nowhere to put anything
/// that is neither a face nor a voice. Rank is exactly such a thing.
struct IdentityRecord
{
    /// The key in the manifest. Stable, used for cache keys and for matching
    /// against the two galleries.
    QString id;

    /// What the overlay writes under the box. Defaults to the id.
    QString name;

    /// Absolute, resolved against the directory holding the manifest. Empty
    /// when this person has no enrolled face, which is allowed: a voice alone
    /// is still a person.
    QString facePath;
    QString voicePath;

    /// Rank, zero being the top. Kerfur looks at the lowest ranked person in
    /// the room and takes commands only from rank zero, so this is the field
    /// that decides who it is talking to when several people are present.
    int priority{0};
};

/// Every declared identity, read from one manifest.
///
/// Paths inside the manifest are relative to the manifest itself, so a gallery
/// is one self contained directory that can be moved, copied to a Pi or checked
/// into a repository without a single path being rewritten.
class IdentityRegistry
{
public:
    /// The rank of somebody who is not in the manifest. Larger than any rank
    /// that can be written there, so an unknown person sorts after every known
    /// one without any call site special casing them.
    static constexpr int unknownPriority{1'000'000};

    /// The manifest file name, looked for in the gallery directory.
    static QString manifestFileName();

    /// Reads `directory/ids.json`. When there is no manifest, falls back to the
    /// older convention -- `name.png` beside `name.mp3`, or the same under
    /// `faces/` and `voices/` -- so a gallery that predates this file keeps
    /// working, at unknown rank.
    ///
    /// A malformed manifest is reported through `problem()` and yields the
    /// fallback rather than an empty registry: refusing to recognise anybody
    /// because a comma is in the wrong place is the least useful of the
    /// available reactions.
    static IdentityRegistry load(const QString &directory);

    QList<IdentityRecord> records() const { return m_records; }
    bool isEmpty() const { return m_records.isEmpty(); }
    qsizetype size() const { return m_records.size(); }

    /// The directory the manifest was read from, or the one that was scanned.
    QString directory() const { return m_directory; }

    /// Empty when everything parsed. Otherwise a sentence naming what did not.
    QString problem() const { return m_problem; }

    /// True when an `ids.json` was found and parsed. False means these records
    /// came from the directory scan, so ranks are all unknown.
    bool hasManifest() const { return m_hasManifest; }

    /// By id, then by display name, both case insensitively. The two galleries
    /// report whichever of the two they were enrolled under and a caller should
    /// not have to know which.
    ///
    /// Returns a copy rather than a pointer into the list. A record is four
    /// strings and an int; a pointer would be marginally cheaper and would
    /// dangle the moment somebody wrote
    /// `IdentityRegistry::load(dir).find("kidev")`, which is the most natural
    /// way to write it.
    std::optional<IdentityRecord> find(const QString &nameOrId) const;

    /// `unknownPriority` for anybody not declared, including the empty name
    /// that means "no match". Never fails, because every consumer of a rank
    /// needs an answer for a stranger too.
    int priorityOf(const QString &nameOrId) const;

    /// The ids, sorted.
    QStringList names() const;

    /// The `(name, path)` pairs the face gallery enrols, skipping records
    /// without a face file. `name` is the id, so both galleries key on the same
    /// string and a person recognised by either is the same person.
    QList<QPair<QString, QString>> faceEntries() const;
    QList<QPair<QString, QString>> voiceEntries() const;

private:
    static IdentityRegistry scanDirectory(const QString &directory);

    QList<IdentityRecord> m_records;
    QString m_directory;
    QString m_problem;
    bool m_hasManifest{false};
};

} // namespace Kerfur

#endif // KERFUR_IDENTITYREGISTRY_H
