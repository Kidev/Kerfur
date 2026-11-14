// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "identitylibrary.h"

#include "identitydirectory.h"
#include "identityregistry.h"
#include "wavfile.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QVariantMap>

#include <algorithm>

#ifdef Q_OS_WASM
#include <QFileDialog>

#include <emscripten.h>
#endif

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(libraryLog, "kerfur.core.identitylibrary")

const QStringList &voiceSuffixes()
{
    static const QStringList suffixes{QStringLiteral("wav"),
                                      QStringLiteral("mp3"),
                                      QStringLiteral("flac"),
                                      QStringLiteral("ogg"),
                                      QStringLiteral("m4a")};
    return suffixes;
}

/// The relative sample paths one manifest entry holds for one kind: the single
/// file under `kind` (`face`, `voice`), which an older manifest writes, then
/// the list under `folder` (`faces`, `voices`), in order and without repeats.
QStringList samplesOf(const QJsonObject &entry, const QString &kind, const QString &folder)
{
    QStringList samples;
    const QString single{entry.value(kind).toString()};
    if (!single.isEmpty()) {
        samples.append(single);
    }
    // Copy initialization: a braced QJsonArray from a QJsonValue picks the
    // initializer list constructor and nests the array one level down.
    const QJsonArray listed = entry.value(folder).toArray();
    for (const QJsonValue &value : listed) {
        const QString path{value.toString()};
        if (!path.isEmpty() && !samples.contains(path)) {
            samples.append(path);
        }
    }
    return samples;
}

bool holdsSamples(const QJsonObject &entry)
{
    return !samplesOf(entry, QStringLiteral("face"), QStringLiteral("faces")).isEmpty() ||
           !samplesOf(entry, QStringLiteral("voice"), QStringLiteral("voices")).isEmpty();
}

QString localPath(const QUrl &file)
{
    if (file.isLocalFile()) {
        return file.toLocalFile();
    }
    // A bare path handed over from C++ or typed into a binding.
    if (file.scheme().isEmpty()) {
        return file.path();
    }
    return {};
}

#ifdef Q_OS_WASM
/// The library the web build's storage callbacks report to. Set before the
/// mount is started, because a mount that fails reports synchronously, from
/// inside the constructor, where `instance()` is not yet safe to call.
IdentityLibrary *storageOwner{nullptr};
#endif

} // namespace

} // namespace Kerfur

#ifdef Q_OS_WASM

// The directory is mounted on IndexedDB and read back once. Writes before that
// first read completes would be lost to it, which is what `ready` guards.
EM_JS(void, kerfurMountIdentityStorage, (const char *path), {
    const directory = UTF8ToString(path);
    try {
        FS.mkdirTree(directory);
        FS.mount(IDBFS, {}, directory);
    } catch (error) {
        console.warn("identities: cannot mount IndexedDB at " + directory + ": " + error);
        _kerfurIdentityStorageLoaded(0);
        return;
    }
    FS.syncfs(true, function(error) {
        if (error) {
            console.warn("identities: cannot read IndexedDB: " + error);
        }
        _kerfurIdentityStorageLoaded(error ? 0 : 1);
    });
})

EM_JS(void, kerfurPersistIdentityStorage, (), {
    FS.syncfs(false, function(error) {
        if (error) {
            console.warn("identities: cannot write IndexedDB: " + error);
        }
        _kerfurIdentityStoragePersisted(error ? 0 : 1);
    });
})

extern "C" EMSCRIPTEN_KEEPALIVE void kerfurIdentityStorageLoaded(int loaded)
{
    if (Kerfur::storageOwner != nullptr) {
        QMetaObject::invokeMethod(Kerfur::storageOwner,
                                  "applyStorageLoaded",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, loaded != 0));
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void kerfurIdentityStoragePersisted(int persisted)
{
    if (Kerfur::storageOwner != nullptr) {
        QMetaObject::invokeMethod(Kerfur::storageOwner,
                                  "applyStoragePersisted",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, persisted != 0));
    }
}

#endif

namespace Kerfur {

IdentityLibrary::IdentityLibrary(const QString &directory, QObject *parent)
    : QObject{parent}
    , m_directory{directory.isEmpty() ? QString{} : QDir::cleanPath(directory)}
{
#ifdef Q_OS_WASM
    if (!m_directory.isEmpty()) {
        storageOwner = this;
        kerfurMountIdentityStorage(m_directory.toUtf8().constData());
        return;
    }
#endif
    m_ready = !m_directory.isEmpty();
    refreshPeople();
}

IdentityLibrary &IdentityLibrary::instance()
{
    static IdentityLibrary library{IdentityDirectory::writableDirectory()};
    return library;
}

IdentityLibrary *IdentityLibrary::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    // C++ owned: both galleries hold connections to it from their own threads.
    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

QString IdentityLibrary::idFor(const QString &name)
{
    // Compatibility decomposition splits an accented letter into its base and
    // a combining mark, and the mark is then dropped with everything else
    // that is not ASCII.
    const QString folded{name.normalized(QString::NormalizationForm_KD)};

    QString id;
    bool separator{false};
    for (const QChar character : folded) {
        const char16_t code{character.unicode()};
        const bool lower{code >= u'a' && code <= u'z'};
        const bool upper{code >= u'A' && code <= u'Z'};
        const bool digit{code >= u'0' && code <= u'9'};

        if (lower || upper || digit) {
            const bool dash{separator && !id.isEmpty()};
            if (id.size() + (dash ? 2 : 1) > maximumIdLength) {
                break;
            }
            if (dash) {
                id.append(QLatin1Char('-'));
            }
            id.append(upper ? QChar{static_cast<char16_t>(code - u'A' + u'a')} : character);
            separator = false;
        } else if (character.isSpace() || code == u'-' || code == u'_' || code == u'.') {
            separator = true;
        }
    }
    return id;
}

bool IdentityLibrary::browserFiles() const
{
#ifdef Q_OS_WASM
    return true;
#else
    return false;
#endif
}

bool IdentityLibrary::stageFaceImage(const QImage &image, const QString &origin)
{
    if (image.isNull()) {
        return fail(QStringLiteral("There is no picture to take. Is the camera on?"));
    }

    QImage picture{image};
    if (std::max(picture.width(), picture.height()) > maximumFaceSide) {
        picture = picture.scaled(maximumFaceSide,
                                 maximumFaceSide,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    }

    // Always re-encoded, whatever it came in as. One format for the gallery,
    // and whatever metadata the original carried (a location, a camera serial)
    // is not copied into it.
    QByteArray encoded;
    QBuffer buffer{&encoded};
    if (!buffer.open(QIODevice::WriteOnly) || !picture.save(&buffer, "PNG")) {
        return fail(QStringLiteral("The picture from %1 could not be encoded.").arg(origin));
    }

    m_stagedFace = encoded;
    m_stagedFaceLabel = QStringLiteral("%1, %2x%3").arg(origin).arg(picture.width()).arg(
        picture.height());
    emit stagedChanged();
    succeed();
    return true;
}

bool IdentityLibrary::stageFaceData(const QByteArray &data, const QString &origin)
{
    if (data.isEmpty()) {
        return fail(QStringLiteral("%1 is empty.").arg(origin));
    }
    if (data.size() > maximumFaceBytes) {
        return fail(QStringLiteral("%1 is larger than %2 MiB.")
                        .arg(origin)
                        .arg(maximumFaceBytes / (1024 * 1024)));
    }

    QByteArray copy{data};
    QBuffer buffer{&copy};
    if (!buffer.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("%1 could not be read.").arg(origin));
    }
    QImageReader reader{&buffer};
    // A phone photograph is stored sideways with a flag saying so. Read
    // without the flag, the face in it is lying down and the detector does
    // not find it.
    reader.setAutoTransform(true);
    const QImage image{reader.read()};
    if (image.isNull()) {
        return fail(QStringLiteral("%1 is not a picture this build can read: %2")
                        .arg(origin, reader.errorString()));
    }
    return stageFaceImage(image, origin);
}

bool IdentityLibrary::stageFaceFile(const QUrl &file)
{
    const QString path{localPath(file)};
    const QFileInfo info{path};
    if (path.isEmpty() || !info.isFile()) {
        return fail(QStringLiteral("%1 is not a local file.").arg(file.toString()));
    }
    if (info.size() > maximumFaceBytes) {
        return fail(QStringLiteral("%1 is larger than %2 MiB.")
                        .arg(info.fileName())
                        .arg(maximumFaceBytes / (1024 * 1024)));
    }
    QFile source{path};
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("%1 could not be opened.").arg(info.fileName()));
    }
    return stageFaceData(source.readAll(), info.fileName());
}

bool IdentityLibrary::stageVoiceData(const QByteArray &data,
                                     const QString &suffix,
                                     const QString &origin)
{
    const QString container{suffix.toLower()};
    if (!voiceSuffixes().contains(container)) {
        return fail(QStringLiteral("%1 is not a recording the voice gallery reads (%2).")
                        .arg(origin, voiceSuffixes().join(QStringLiteral(", "))));
    }
    if (data.isEmpty()) {
        return fail(QStringLiteral("%1 is empty.").arg(origin));
    }
    if (data.size() > maximumVoiceBytes) {
        return fail(QStringLiteral("%1 is larger than %2 MiB.")
                        .arg(origin)
                        .arg(maximumVoiceBytes / (1024 * 1024)));
    }

    m_stagedVoice = StagedVoice{data, container};
    m_stagedVoiceLabel = origin;
    emit stagedChanged();
    succeed();
    return true;
}

bool IdentityLibrary::stageVoiceFile(const QUrl &file)
{
    const QString path{localPath(file)};
    const QFileInfo info{path};
    if (path.isEmpty() || !info.isFile()) {
        return fail(QStringLiteral("%1 is not a local file.").arg(file.toString()));
    }
    if (info.size() > maximumVoiceBytes) {
        return fail(QStringLiteral("%1 is larger than %2 MiB.")
                        .arg(info.fileName())
                        .arg(maximumVoiceBytes / (1024 * 1024)));
    }
    QFile source{path};
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("%1 could not be opened.").arg(info.fileName()));
    }
    return stageVoiceData(source.readAll(), info.suffix(), info.fileName());
}

bool IdentityLibrary::stageVoiceSamples(const std::vector<float> &samples,
                                        int sampleRate,
                                        const QString &origin,
                                        double minimumSeconds)
{
    if (samples.empty() || sampleRate <= 0) {
        return fail(QStringLiteral("Nothing has been heard yet."));
    }
    const double seconds{static_cast<double>(samples.size()) / sampleRate};
    if (seconds < minimumSeconds) {
        return fail(QStringLiteral("The %1 lasted %2 s. A voice needs at least %3 s of "
                                   "speech to be recognised by.")
                        .arg(origin)
                        .arg(seconds, 0, 'f', 1)
                        .arg(minimumSeconds, 0, 'f', 1));
    }
    return stageVoiceData(WavFile::encodeMono16(samples, sampleRate),
                          QStringLiteral("wav"),
                          QStringLiteral("%1, %2 s").arg(origin).arg(seconds, 0, 'f', 1));
}

void IdentityLibrary::browseFace()
{
#ifdef Q_OS_WASM
    QFileDialog::getOpenFileContent(QStringLiteral("Pictures (*.png *.jpg *.jpeg *.bmp)"),
                                    [this](const QString &fileName, const QByteArray &content) {
                                        // An empty name is a cancelled dialog.
                                        if (!fileName.isEmpty()) {
                                            stageFaceData(content, QFileInfo{fileName}.fileName());
                                        }
                                    });
#else
    fail(QStringLiteral("Choose the picture with the file dialog."));
#endif
}

void IdentityLibrary::browseVoice()
{
#ifdef Q_OS_WASM
    QFileDialog::getOpenFileContent(
        QStringLiteral("Recordings (*.wav *.mp3 *.flac *.ogg *.m4a)"),
        [this](const QString &fileName, const QByteArray &content) {
            if (!fileName.isEmpty()) {
                const QFileInfo info{fileName};
                stageVoiceData(content, info.suffix(), info.fileName());
            }
        });
#else
    fail(QStringLiteral("Choose the recording with the file dialog."));
#endif
}

void IdentityLibrary::clearStaged()
{
    m_stagedFace.clear();
    m_stagedFaceLabel.clear();
    m_stagedVoice = StagedVoice{};
    m_stagedVoiceLabel.clear();
    emit stagedChanged();
}

bool IdentityLibrary::enrol(const QString &name, const QString &title, int priority)
{
    if (m_directory.isEmpty()) {
        return fail(QStringLiteral("This platform offers no writable location for identities."));
    }
    if (!m_ready) {
        return fail(QStringLiteral("Stored identities are still loading. Try again in a moment."));
    }

    const QString displayName{name.simplified().left(maximumTextLength)};
    const QString id{idFor(displayName)};
    if (id.isEmpty()) {
        return fail(QStringLiteral("A name needs at least one letter or digit."));
    }

    std::optional<QJsonObject> manifest{readManifest()};
    if (!manifest.has_value()) {
        return false;
    }

    const bool known{manifest->contains(id)};
    if (!known && m_stagedFace.isEmpty() && m_stagedVoice.data.isEmpty()) {
        return fail(QStringLiteral("Take a picture or a recording of %1 first.").arg(displayName));
    }

    QJsonObject entry{manifest->value(id).toObject()};

    if (!m_stagedFace.isEmpty() && !addSample(entry,
                                              id,
                                              QStringLiteral("face"),
                                              QStringLiteral("faces"),
                                              QStringLiteral("png"),
                                              m_stagedFace)) {
        return false;
    }

    if (!m_stagedVoice.data.isEmpty() && !addSample(entry,
                                                    id,
                                                    QStringLiteral("voice"),
                                                    QStringLiteral("voices"),
                                                    m_stagedVoice.suffix,
                                                    m_stagedVoice.data)) {
        return false;
    }

    entry.insert(QStringLiteral("name"), displayName);
    const QString standing{title.simplified().left(maximumTextLength)};
    if (standing.isEmpty()) {
        entry.remove(QStringLiteral("title"));
    } else {
        entry.insert(QStringLiteral("title"), standing);
    }
    entry.insert(QStringLiteral("priority"), std::clamp(priority, 0, maximumPriority));
    manifest->insert(id, entry);

    if (!writeManifest(*manifest)) {
        return false;
    }

    qCInfo(libraryLog) << (known ? "updated" : "enrolled") << id << "in" << m_directory;
    m_stagedFace.clear();
    m_stagedFaceLabel.clear();
    m_stagedVoice = StagedVoice{};
    m_stagedVoiceLabel.clear();
    emit stagedChanged();
    commit();
    succeed();
    return true;
}

bool IdentityLibrary::remove(const QString &id)
{
    if (m_directory.isEmpty() || !m_ready) {
        return fail(QStringLiteral("Stored identities are not available yet."));
    }

    std::optional<QJsonObject> manifest{readManifest()};
    if (!manifest.has_value()) {
        return false;
    }
    if (id.isEmpty() || !manifest->contains(id)) {
        return fail(QStringLiteral("%1 was not enrolled here.").arg(id));
    }

    const QJsonObject entry{manifest->value(id).toObject()};
    const QStringList samples{samplesOf(entry, QStringLiteral("face"), QStringLiteral("faces")) +
                              samplesOf(entry, QStringLiteral("voice"), QStringLiteral("voices"))};
    for (const QString &sample : samples) {
        removeInside(sample);
    }
    manifest->remove(id);

    if (!writeManifest(*manifest)) {
        return false;
    }

    qCInfo(libraryLog) << "removed" << id << "from" << m_directory;
    commit();
    succeed();
    return true;
}

bool IdentityLibrary::forgetFaces(const QString &id)
{
    return forgetSamples(id, QStringLiteral("face"), QStringLiteral("faces"));
}

bool IdentityLibrary::forgetVoices(const QString &id)
{
    return forgetSamples(id, QStringLiteral("voice"), QStringLiteral("voices"));
}

bool IdentityLibrary::forgetSamples(const QString &id, const QString &kind, const QString &folder)
{
    if (m_directory.isEmpty() || !m_ready) {
        return fail(QStringLiteral("Stored identities are not available yet."));
    }

    std::optional<QJsonObject> manifest{readManifest()};
    if (!manifest.has_value()) {
        return false;
    }
    if (id.isEmpty() || !manifest->contains(id)) {
        return fail(QStringLiteral("%1 was not enrolled here.").arg(id));
    }

    QJsonObject entry{manifest->value(id).toObject()};
    for (const QString &sample : samplesOf(entry, kind, folder)) {
        removeInside(sample);
    }
    entry.remove(kind);
    entry.remove(folder);

    // A record with no sample left recognises nobody, and keeping it would
    // list a person the panel can no longer teach anything about.
    const bool emptied{!holdsSamples(entry)};
    if (emptied) {
        manifest->remove(id);
    } else {
        manifest->insert(id, entry);
    }

    if (!writeManifest(*manifest)) {
        return false;
    }

    qCInfo(libraryLog) << "forgot every" << kind << "of" << id
                       << (emptied ? "and the person with them" : "");
    commit();
    succeed();
    return true;
}

bool IdentityLibrary::addSample(QJsonObject &entry,
                                const QString &id,
                                const QString &kind,
                                const QString &folder,
                                const QString &suffix,
                                const QByteArray &data)
{
    QStringList samples{samplesOf(entry, kind, folder)};

    // The first number nothing answers to, on disk or in the manifest, so a
    // sample is never written over another one or over a file left behind.
    QString file;
    for (int number{1}; file.isEmpty(); ++number) {
        const QString candidate{
            QStringLiteral("%1/%2-%3.%4").arg(folder, id, QString::number(number), suffix)};
        if (!samples.contains(candidate) && !QFileInfo::exists(resolveInside(candidate))) {
            file = candidate;
        }
    }
    if (!writeInside(file, data)) {
        return false;
    }
    samples.append(file);

    while (samples.size() > maximumSamples) {
        removeInside(samples.takeFirst());
    }

    entry.remove(kind);
    entry.insert(folder, QJsonArray::fromStringList(samples));
    return true;
}

void IdentityLibrary::applyStorageLoaded(bool loaded)
{
    if (!loaded) {
        // Still usable for this session, only not across a reload. Saying so
        // is better than refusing to enrol anybody.
        fail(QStringLiteral("Browser storage is unavailable; people added now are forgotten "
                            "on reload."));
    }
    m_ready = true;
    emit readyChanged();
    refreshPeople();
    emit galleryChanged();
}

void IdentityLibrary::applyStoragePersisted(bool persisted)
{
    if (!persisted) {
        fail(QStringLiteral("The browser refused to store the change; it lasts until reload."));
    }
}

std::optional<QJsonObject> IdentityLibrary::readManifest()
{
    QFile file{QDir{m_directory}.absoluteFilePath(IdentityRegistry::manifestFileName())};
    if (!file.exists()) {
        return QJsonObject{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("%1 could not be read.").arg(file.fileName()));
        return std::nullopt;
    }

    QJsonParseError error{};
    const QJsonDocument document{QJsonDocument::fromJson(file.readAll(), &error)};
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("identities")).isObject()
        || document.object().value(QStringLiteral("identities")).isArray()) {
        fail(QStringLiteral("%1 has been edited by hand into a form this panel does not "
                            "rewrite. Edit it by hand, or move it away.")
                 .arg(file.fileName()));
        return std::nullopt;
    }
    return document.object();
}

bool IdentityLibrary::writeManifest(const QJsonObject &manifest)
{
    return writeInside(IdentityRegistry::manifestFileName(),
                       QJsonDocument{manifest}.toJson(QJsonDocument::Indented));
}

QString IdentityLibrary::resolveInside(const QString &relative) const
{
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)) {
        return {};
    }
    const QString resolved{QDir::cleanPath(m_directory + QLatin1Char('/') + relative)};
    if (!resolved.startsWith(m_directory + QLatin1Char('/'))) {
        return {};
    }
    return resolved;
}

bool IdentityLibrary::writeInside(const QString &relative, const QByteArray &data)
{
    const QString path{resolveInside(relative)};
    if (path.isEmpty()) {
        return fail(QStringLiteral("%1 is outside %2.").arg(relative, m_directory));
    }
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath())) {
        return fail(QStringLiteral("%1 could not be created.").arg(QFileInfo{path}.absolutePath()));
    }

    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return fail(QStringLiteral("%1 could not be written: %2").arg(path, file.errorString()));
    }
    return true;
}

void IdentityLibrary::removeInside(const QString &relative)
{
    const QString path{resolveInside(relative)};
    if (path.isEmpty()) {
        if (!relative.isEmpty()) {
            qCWarning(libraryLog) << "not deleting" << relative << "outside" << m_directory;
        }
        return;
    }
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qCWarning(libraryLog) << "could not delete" << path;
    }
}

bool IdentityLibrary::fail(const QString &problem)
{
    qCInfo(libraryLog) << problem;
    if (m_problem != problem) {
        m_problem = problem;
        emit problemChanged();
    }
    return false;
}

void IdentityLibrary::succeed()
{
    if (!m_problem.isEmpty()) {
        m_problem.clear();
        emit problemChanged();
    }
}

void IdentityLibrary::refreshPeople()
{
    QVariantList people;
    if (!m_directory.isEmpty()
        && QFileInfo::exists(
            QDir{m_directory}.absoluteFilePath(IdentityRegistry::manifestFileName()))) {
        const IdentityRegistry registry{IdentityRegistry::load(m_directory)};
        for (const IdentityRecord &record : registry.records()) {
            people.append(QVariantMap{
                {QStringLiteral("id"), record.id},
                {QStringLiteral("name"), record.name},
                {QStringLiteral("title"), record.title},
                {QStringLiteral("priority"), record.priority},
                {QStringLiteral("standing"), registry.standingOf(record.id)},
                {QStringLiteral("hasFace"), !record.facePaths.isEmpty()},
                {QStringLiteral("hasVoice"), !record.voicePaths.isEmpty()},
                {QStringLiteral("faceCount"), record.facePaths.size()},
                {QStringLiteral("voiceCount"), record.voicePaths.size()},
            });
        }
    }
    m_people = people;
    emit peopleChanged();
}

void IdentityLibrary::persist()
{
#ifdef Q_OS_WASM
    kerfurPersistIdentityStorage();
#endif
}

void IdentityLibrary::commit()
{
    refreshPeople();
    persist();
    emit galleryChanged();
}

} // namespace Kerfur
