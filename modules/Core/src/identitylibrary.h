// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_IDENTITYLIBRARY_H
#define KERFUR_IDENTITYLIBRARY_H

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Everybody Kerfur has been taught, kept in
/// `IdentityDirectory::writableDirectory()`. A fresh installation knows nobody.
///
/// The directory is an ordinary gallery: an `ids.json` that this class writes,
/// with `faces/` and `voices/` beside it. The two galleries read it through
/// `IdentityDirectory::galleries()` and re-enrol whenever `galleryChanged` is
/// emitted, so a person saved here is recognised without a restart, and the
/// directory is what carries them into the next session.
///
/// Teaching somebody is two steps. A face and a voice are staged first (from
/// the camera, from the last sentence heard, or from a file), then `enrol`
/// adds both to that person as one more sample each. Nothing reaches the disk
/// until `enrol`, so a picture taken by mistake is discarded by staging
/// another one. Every sample is enrolled on its own, so teaching the same
/// person again in other light, from another angle or in a noisier room is
/// what makes them easier to recognise.
///
/// On the web build the directory is an IndexedDB mount. It is read
/// asynchronously after start up, `ready` stays false until it has been, and
/// every write is refused until then: writing into the mount before the first
/// sync would be overwritten by it.
class IdentityLibrary : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Identities)
    QML_SINGLETON

    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    /// True when files are chosen through the browser rather than a QML
    /// FileDialog, which on the web build has no file system to show.
    Q_PROPERTY(bool browserFiles READ browserFiles CONSTANT)
    /// One map per person enrolled here: `id`, `name`, `title`, `priority`,
    /// `standing`, `hasFace`, `hasVoice`, `faceCount`, `voiceCount`. People
    /// from a gallery configured by hand are not listed: they cannot be
    /// changed from here.
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)
    /// What is staged, described for a person, or empty.
    Q_PROPERTY(QString stagedFace READ stagedFace NOTIFY stagedChanged)
    Q_PROPERTY(QString stagedVoice READ stagedVoice NOTIFY stagedChanged)
    /// `maximumSamples`, for the panel to quote.
    Q_PROPERTY(int sampleLimit READ sampleLimit CONSTANT)
    /// The last refusal or failure, cleared by the next call that succeeds.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    /// Larger files are refused before they are read.
    static constexpr qsizetype maximumFaceBytes{32 * 1024 * 1024};
    static constexpr qsizetype maximumVoiceBytes{64 * 1024 * 1024};
    /// A staged picture is reduced to this on its longer side.
    static constexpr int maximumFaceSide{1920};
    static constexpr qsizetype maximumIdLength{48};
    static constexpr qsizetype maximumTextLength{64};
    /// Ranks accepted by `enrol`. Anything above is clamped, so a record
    /// written here always sorts before a stranger.
    static constexpr int maximumPriority{999};
    /// Pictures, and separately recordings, kept per person. Past this the
    /// oldest is deleted, so a person taught every day keeps their recent
    /// samples and the directory stays bounded.
    static constexpr qsizetype maximumSamples{10};

    /// `directory` is created on the first write. The web build mounts it on
    /// IndexedDB here, which is why the application has one instance.
    explicit IdentityLibrary(const QString &directory, QObject *parent = nullptr);

    /// The application's instance, over `IdentityDirectory::writableDirectory()`.
    static IdentityLibrary &instance();
    static IdentityLibrary *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// The manifest id for a display name: lower case ASCII letters, digits and
    /// single dashes, accents folded, at most `maximumIdLength` long. Empty
    /// when nothing usable is left. The id also names the files, so anything
    /// that could leave the directory is gone by construction.
    static QString idFor(const QString &name);

    bool isReady() const { return m_ready; }
    QString directory() const { return m_directory; }
    bool browserFiles() const;
    QVariantList people() const { return m_people; }
    QString stagedFace() const { return m_stagedFaceLabel; }
    QString stagedVoice() const { return m_stagedVoiceLabel; }
    QString problem() const { return m_problem; }
    int sampleLimit() const { return static_cast<int>(maximumSamples); }

    /// A picture already in memory, such as the camera frame. `origin` is how
    /// the panel describes it.
    Q_INVOKABLE bool stageFaceImage(const QImage &image, const QString &origin);
    Q_INVOKABLE bool stageFaceFile(const QUrl &file);
    Q_INVOKABLE bool stageVoiceFile(const QUrl &file);

    /// The browser's own file picker, on the web build. The file is staged when
    /// the person has picked one; elsewhere this only reports that it is
    /// unavailable.
    Q_INVOKABLE void browseFace();
    Q_INVOKABLE void browseVoice();

    bool stageFaceData(const QByteArray &data, const QString &origin);
    /// `suffix` names the container, and must be one the voice gallery reads.
    bool stageVoiceData(const QByteArray &data, const QString &suffix, const QString &origin);
    /// Mono samples in [-1, 1], kept as a 16 bit WAVE. Refused when shorter
    /// than `minimumSeconds`.
    bool stageVoiceSamples(const std::vector<float> &samples,
                           int sampleRate,
                           const QString &origin,
                           double minimumSeconds = 0.0);

    Q_INVOKABLE void clearStaged();

    /// Adds the staged face and voice to `idFor(name)` as one more sample each
    /// and records the person in the manifest. An id already enrolled here
    /// keeps every earlier sample (up to `maximumSamples` of each, oldest
    /// dropped first) and takes the new name, title and rank. A new id needs at
    /// least one of the two staged; a known one may be saved with neither, to
    /// change only its title or rank.
    Q_INVOKABLE bool enrol(const QString &name, const QString &title, int priority);

    /// Forgets somebody enrolled here, and deletes their files.
    Q_INVOKABLE bool remove(const QString &id);

    /// Deletes every picture, or every recording, of somebody enrolled here.
    /// A person left with neither is forgotten entirely.
    Q_INVOKABLE bool forgetFaces(const QString &id);
    Q_INVOKABLE bool forgetVoices(const QString &id);

signals:
    void readyChanged();
    void peopleChanged();
    void stagedChanged();
    void problemChanged();

    /// The manifest or one of its files changed. Both galleries re-enrol on
    /// it, on their own threads.
    void galleryChanged();

private slots:
    /// The web build's first read of IndexedDB has finished.
    void applyStorageLoaded(bool loaded);
    void applyStoragePersisted(bool persisted);

private:
    struct StagedVoice
    {
        QByteArray data;
        QString suffix;
    };

    /// The manifest as a map, or nothing when it exists in a shape this class
    /// does not write (a list, or wrapped in `identities`) or does not parse.
    /// Such a file was written by hand and is left alone.
    std::optional<QJsonObject> readManifest();
    bool writeManifest(const QJsonObject &manifest);

    /// Writes `data` as a new sample of `id` under `folder` and appends it to
    /// `entry[folder]`, folding an older single `kind` file into the list and
    /// deleting the oldest samples past `maximumSamples`.
    bool addSample(QJsonObject &entry,
                   const QString &id,
                   const QString &kind,
                   const QString &folder,
                   const QString &suffix,
                   const QByteArray &data);

    /// The shared body of `forgetFaces` and `forgetVoices`.
    bool forgetSamples(const QString &id, const QString &kind, const QString &folder);

    /// Writes `data` to `relative` inside the directory, atomically.
    bool writeInside(const QString &relative, const QByteArray &data);
    /// Deletes `relative` when it resolves inside the directory, and does
    /// nothing otherwise. The manifest is a file a person can edit, so a path
    /// read from it is not trusted to stay inside.
    void removeInside(const QString &relative);
    QString resolveInside(const QString &relative) const;

    bool fail(const QString &problem);
    void succeed();
    void refreshPeople();
    /// Flushes the directory to IndexedDB on the web build. Nothing elsewhere.
    void persist();
    void commit();

    QString m_directory;
    QVariantList m_people;
    QByteArray m_stagedFace;
    QString m_stagedFaceLabel;
    StagedVoice m_stagedVoice;
    QString m_stagedVoiceLabel;
    QString m_problem;
    bool m_ready{false};
};

} // namespace Kerfur

#endif // KERFUR_IDENTITYLIBRARY_H
