// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identityregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(registryLog, "kerfur.core.identities")

const QStringList &faceFilters()
{
    static const QStringList filters{QStringLiteral("*.png"),
                                     QStringLiteral("*.jpg"),
                                     QStringLiteral("*.jpeg"),
                                     QStringLiteral("*.bmp")};
    return filters;
}

const QStringList &voiceFilters()
{
    static const QStringList filters{QStringLiteral("*.mp3"),
                                     QStringLiteral("*.wav"),
                                     QStringLiteral("*.flac"),
                                     QStringLiteral("*.ogg"),
                                     QStringLiteral("*.m4a")};
    return filters;
}

/// Removes a comma that is followed only by whitespace and a closing brace.
///
/// Strict JSON forbids it and this manifest is written by hand, which is the
/// one situation where a parser that dies on a trailing comma costs more than
/// it protects. Only commas outside string literals are considered, so a name
/// containing `,}` survives.
QByteArray stripTrailingCommas(const QByteArray &source)
{
    QByteArray result;
    result.reserve(source.size());

    bool inString{false};
    bool escaped{false};
    for (qsizetype index{0}; index < source.size(); ++index) {
        const char character{source.at(index)};

        if (inString) {
            result.append(character);
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
            result.append(character);
            continue;
        }

        if (character == ',') {
            qsizetype lookahead{index + 1};
            while (lookahead < source.size()
                   && QChar::fromLatin1(source.at(lookahead)).isSpace()) {
                ++lookahead;
            }
            if (lookahead < source.size()
                && (source.at(lookahead) == '}' || source.at(lookahead) == ']')) {
                continue;
            }
        }

        result.append(character);
    }

    return result;
}

/// A rank written either as a number or as a string. The manifest is hand
/// edited and `"priority": "0"` is what a person writes when every other value
/// on the line is quoted.
int readPriority(const QJsonValue &value, bool *ok)
{
    if (ok != nullptr) {
        *ok = true;
    }
    if (value.isDouble()) {
        return static_cast<int>(value.toDouble());
    }
    if (value.isString()) {
        bool converted{false};
        const int parsed{value.toString().trimmed().toInt(&converted)};
        if (converted) {
            return parsed;
        }
    }
    if (value.isUndefined() || value.isNull()) {
        return IdentityRegistry::unknownPriority;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return IdentityRegistry::unknownPriority;
}

/// Resolves one declared path against the manifest's own directory. An absolute
/// path is left alone, which is what lets a shared gallery live outside the
/// application tree.
QString resolvePath(const QDir &base, const QString &declared)
{
    if (declared.isEmpty()) {
        return {};
    }
    const QFileInfo info{QDir::isAbsolutePath(declared) ? declared
                                                        : base.absoluteFilePath(declared)};
    return info.absoluteFilePath();
}

/// The samples one manifest entry declares: the single file under `single`,
/// which is what an older manifest writes, followed by the list under `list`,
/// in order and without repeats.
QStringList readSamples(const QDir &base,
                        const QJsonObject &entry,
                        const QString &single,
                        const QString &list)
{
    QStringList paths;
    const QString one{resolvePath(base, entry.value(single).toString())};
    if (!one.isEmpty()) {
        paths.append(one);
    }
    // Copy initialization: a braced QJsonArray from a QJsonValue picks the
    // initializer list constructor and nests the array one level down.
    const QJsonArray many = entry.value(list).toArray();
    for (const QJsonValue &value : many) {
        const QString path{resolvePath(base, value.toString())};
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.append(path);
        }
    }
    return paths;
}

/// The first existing file matching `stem` under `directory` with any of
/// `filters`. Used only by the fallback scan and by the manifest's implicit
/// paths, so that a record may omit `face` when the file sits where convention
/// puts it.
QString findByStem(const QDir &directory, const QString &stem, const QStringList &filters)
{
    if (!directory.exists()) {
        return {};
    }
    QStringList stemFilters;
    stemFilters.reserve(filters.size());
    for (const QString &filter : filters) {
        QString pattern{filter};
        stemFilters.append(pattern.replace(QLatin1Char('*'), stem));
    }
    const QFileInfoList matches{
        directory.entryInfoList(stemFilters, QDir::Files | QDir::Readable, QDir::Name)};
    return matches.isEmpty() ? QString{} : matches.first().absoluteFilePath();
}

/// One record from one manifest entry. `key` is the object key when the
/// manifest is a map and is empty when it is an array, in which case the entry
/// must carry its own `id` or `name`.
bool readRecord(const QDir &base,
                const QString &key,
                const QJsonObject &entry,
                IdentityRecord &record,
                QString &problem)
{
    record.id = entry.value(QStringLiteral("id")).toString(key).trimmed();
    if (record.id.isEmpty()) {
        record.id = entry.value(QStringLiteral("name")).toString().trimmed();
    }
    if (record.id.isEmpty()) {
        problem = QStringLiteral("an entry has neither a key nor an id");
        return false;
    }

    record.name = entry.value(QStringLiteral("name")).toString(record.id).trimmed();
    if (record.name.isEmpty()) {
        record.name = record.id;
    }

    record.title = entry.value(QStringLiteral("title")).toString().trimmed();

    record.facePaths = readSamples(base, entry, QStringLiteral("face"), QStringLiteral("faces"));
    record.voicePaths = readSamples(base, entry, QStringLiteral("voice"), QStringLiteral("voices"));

    // The conventional locations, for a record that names neither. Both the
    // subdirectory layout and the flat one, because the flat one is what every
    // gallery written before this manifest looks like.
    if (record.facePaths.isEmpty()) {
        QString found{findByStem(QDir{base.absoluteFilePath(QStringLiteral("faces"))},
                                 record.id,
                                 faceFilters())};
        if (found.isEmpty()) {
            found = findByStem(base, record.id, faceFilters());
        }
        if (!found.isEmpty()) {
            record.facePaths.append(found);
        }
    }
    if (record.voicePaths.isEmpty()) {
        QString found{findByStem(QDir{base.absoluteFilePath(QStringLiteral("voices"))},
                                 record.id,
                                 voiceFilters())};
        if (found.isEmpty()) {
            found = findByStem(base, record.id, voiceFilters());
        }
        if (!found.isEmpty()) {
            record.voicePaths.append(found);
        }
    }

    bool priorityOk{true};
    record.priority = readPriority(entry.value(QStringLiteral("priority")), &priorityOk);
    if (!priorityOk) {
        problem = QStringLiteral("%1 has a priority that is not a number").arg(record.id);
        record.priority = IdentityRegistry::unknownPriority;
    }

    // A declared file that is not there is worth saying out loud. It is not
    // fatal: the other samples may still enrol, and part of a person is more
    // useful than none.
    const auto dropMissing = [&record, &problem](QStringList &paths, const QString &kind) {
        for (qsizetype index{paths.size() - 1}; index >= 0; --index) {
            if (!QFileInfo::exists(paths.at(index))) {
                problem = QStringLiteral("%1 declares a %2 at %3 which does not exist")
                              .arg(record.id, kind, paths.at(index));
                paths.removeAt(index);
            }
        }
    };
    dropMissing(record.facePaths, QStringLiteral("face"));
    dropMissing(record.voicePaths, QStringLiteral("voice"));

    return true;
}

/// Rank first, then id, so that every consumer that walks the list in order
/// walks it in rank order without sorting again.
void sortByRank(QList<IdentityRecord> &records)
{
    std::sort(records.begin(),
              records.end(),
              [](const IdentityRecord &left, const IdentityRecord &right) {
                  if (left.priority != right.priority) {
                      return left.priority < right.priority;
                  }
                  return left.id.compare(right.id, Qt::CaseInsensitive) < 0;
              });
}

} // namespace

QString IdentityRegistry::manifestFileName()
{
    return QStringLiteral("ids.json");
}

IdentityRegistry IdentityRegistry::load(const QString &directory)
{
    IdentityRegistry registry;
    registry.m_directory = directory;

    if (directory.isEmpty()) {
        return registry;
    }

    const QDir base{directory};
    const QString manifestPath{base.absoluteFilePath(manifestFileName())};
    QFile manifest{manifestPath};
    if (!manifest.exists()) {
        IdentityRegistry scanned{scanDirectory(directory)};
        scanned.m_problem = QStringLiteral("no %1 in %2; enrolling by file name")
                                .arg(manifestFileName(), directory);
        return scanned;
    }

    if (!manifest.open(QIODevice::ReadOnly)) {
        IdentityRegistry scanned{scanDirectory(directory)};
        scanned.m_problem = QStringLiteral("%1 could not be read").arg(manifestPath);
        return scanned;
    }

    QJsonParseError error{};
    const QByteArray raw{manifest.readAll()};
    QJsonDocument document{QJsonDocument::fromJson(raw, &error)};
    if (error.error != QJsonParseError::NoError) {
        // One retry with hand editing forgiven, and only then a failure.
        QJsonParseError retry{};
        document = QJsonDocument::fromJson(stripTrailingCommas(raw), &retry);
        if (retry.error != QJsonParseError::NoError) {
            IdentityRegistry scanned{scanDirectory(directory)};
            scanned.m_problem = QStringLiteral("%1 is not valid JSON: %2 at offset %3")
                                    .arg(manifestPath, error.errorString())
                                    .arg(error.offset);
            qCWarning(registryLog) << scanned.m_problem;
            return scanned;
        }
        qCWarning(registryLog) << manifestPath << "has a trailing comma; parsed anyway";
    }

    // Three accepted shapes, because the manifest is written by hand and all
    // three are what a person reaches for: a bare map of id to record, the same
    // under an `identities` key, and a list of records carrying their own ids.
    QJsonValue body{document.isArray() ? QJsonValue{document.array()}
                                       : QJsonValue{document.object()}};
    if (body.isObject()) {
        const QJsonObject object{body.toObject()};
        if (object.contains(QStringLiteral("identities"))) {
            body = object.value(QStringLiteral("identities"));
        }
    }

    QStringList problems;
    const auto append = [&](const QString &key, const QJsonValue &value) {
        if (!value.isObject()) {
            problems.append(QStringLiteral("%1 is not an object")
                                .arg(key.isEmpty() ? QStringLiteral("an entry") : key));
            return;
        }
        IdentityRecord record;
        QString problem;
        if (readRecord(base, key, value.toObject(), record, problem)) {
            registry.m_records.append(record);
        }
        if (!problem.isEmpty()) {
            problems.append(problem);
        }
    };

    if (body.isObject()) {
        const QJsonObject entries{body.toObject()};
        for (auto entry{entries.constBegin()}; entry != entries.constEnd(); ++entry) {
            append(entry.key(), entry.value());
        }
    } else if (body.isArray()) {
        for (const QJsonValue &value : body.toArray()) {
            append(QString{}, value);
        }
    } else {
        problems.append(QStringLiteral("%1 holds neither a map nor a list of identities")
                            .arg(manifestPath));
    }

    registry.m_hasManifest = true;
    registry.m_problem = problems.join(QStringLiteral("; "));
    sortByRank(registry.m_records);
    return registry;
}

IdentityRegistry IdentityRegistry::load(const QStringList &directories)
{
    if (directories.size() == 1) {
        return load(directories.first());
    }

    IdentityRegistry merged;
    QStringList problems;
    for (const QString &directory : directories) {
        const IdentityRegistry layer{load(directory)};
        if (merged.m_directory.isEmpty()) {
            merged.m_directory = layer.m_directory;
        }
        merged.m_hasManifest = merged.m_hasManifest || layer.m_hasManifest;
        if (!layer.m_problem.isEmpty()) {
            problems.append(layer.m_problem);
        }
        for (const IdentityRecord &record : layer.m_records) {
            const auto existing{std::find_if(merged.m_records.begin(),
                                             merged.m_records.end(),
                                             [&record](const IdentityRecord &candidate) {
                                                 return candidate.id.compare(record.id,
                                                                             Qt::CaseInsensitive)
                                                        == 0;
                                             })};
            if (existing != merged.m_records.end()) {
                *existing = record;
            } else {
                merged.m_records.append(record);
            }
        }
    }

    merged.m_problem = problems.join(QStringLiteral("; "));
    sortByRank(merged.m_records);
    return merged;
}

IdentityRegistry IdentityRegistry::scanDirectory(const QString &directory)
{
    IdentityRegistry registry;
    registry.m_directory = directory;

    const QDir base{directory};
    if (!base.exists()) {
        return registry;
    }

    const QDir faces{base.absoluteFilePath(QStringLiteral("faces"))};
    const QDir voices{base.absoluteFilePath(QStringLiteral("voices"))};

    QSet<QString> stems;
    const auto collect = [&stems](const QDir &from, const QStringList &filters) {
        if (!from.exists()) {
            return;
        }
        for (const QFileInfo &entry :
             from.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name)) {
            stems.insert(entry.completeBaseName());
        }
    };
    collect(base, faceFilters());
    collect(base, voiceFilters());
    collect(faces, faceFilters());
    collect(voices, voiceFilters());

    QStringList ordered{stems.values()};
    ordered.sort();
    for (const QString &stem : std::as_const(ordered)) {
        IdentityRecord record;
        record.id = stem;
        record.name = stem;
        record.priority = unknownPriority;
        QString face{findByStem(faces, stem, faceFilters())};
        if (face.isEmpty()) {
            face = findByStem(base, stem, faceFilters());
        }
        if (!face.isEmpty()) {
            record.facePaths.append(face);
        }
        QString voice{findByStem(voices, stem, voiceFilters())};
        if (voice.isEmpty()) {
            voice = findByStem(base, stem, voiceFilters());
        }
        if (!voice.isEmpty()) {
            record.voicePaths.append(voice);
        }
        registry.m_records.append(record);
    }

    return registry;
}

std::optional<IdentityRecord> IdentityRegistry::find(const QString &nameOrId) const
{
    if (nameOrId.isEmpty()) {
        return std::nullopt;
    }
    for (const IdentityRecord &record : m_records) {
        if (record.id.compare(nameOrId, Qt::CaseInsensitive) == 0) {
            return record;
        }
    }
    for (const IdentityRecord &record : m_records) {
        if (record.name.compare(nameOrId, Qt::CaseInsensitive) == 0) {
            return record;
        }
    }
    return std::nullopt;
}

int IdentityRegistry::priorityOf(const QString &nameOrId) const
{
    const std::optional<IdentityRecord> record{find(nameOrId)};
    return record.has_value() ? record->priority : unknownPriority;
}

QString IdentityRegistry::titleOf(const QString &nameOrId) const
{
    const std::optional<IdentityRecord> record{find(nameOrId)};
    return record.has_value() ? record->title : QString{};
}

QString IdentityRegistry::standingOf(const QString &nameOrId) const
{
    const std::optional<IdentityRecord> record{find(nameOrId)};
    if (!record.has_value()) {
        return QStringLiteral("unranked");
    }
    if (!record->title.isEmpty()) {
        return record->title;
    }
    return record->priority == unknownPriority ? QStringLiteral("unranked")
                                               : QStringLiteral("rank %1").arg(record->priority);
}

QStringList IdentityRegistry::names() const
{
    QStringList names;
    names.reserve(m_records.size());
    for (const IdentityRecord &record : m_records) {
        names.append(record.id);
    }
    names.sort();
    return names;
}

QList<QPair<QString, QString>> IdentityRegistry::faceEntries() const
{
    QList<QPair<QString, QString>> entries;
    for (const IdentityRecord &record : m_records) {
        for (const QString &path : record.facePaths) {
            entries.append({record.id, path});
        }
    }
    return entries;
}

QList<QPair<QString, QString>> IdentityRegistry::voiceEntries() const
{
    QList<QPair<QString, QString>> entries;
    for (const IdentityRecord &record : m_records) {
        for (const QString &path : record.voicePaths) {
            entries.append({record.id, path});
        }
    }
    return entries;
}

} // namespace Kerfur
