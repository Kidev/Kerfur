// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "voiceplayer.h"

#include <QLoggingCategory>

#ifdef Q_OS_WASM
#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <emscripten.h>
#else
#include <QAudioOutput>
#include <QMediaPlayer>
#endif

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(playerLog, "kerfur.core.voiceplayer")

#ifdef Q_OS_WASM
/// Recordings larger than this are refused before they are read: the whole
/// file is copied into a browser blob.
constexpr qint64 maximumRecordingBytes{64 * 1024 * 1024};

/// The player the browser reports to. The page plays one recording at a time,
/// so the last player to start one is the only one an end can belong to.
VoicePlayer *playbackOwner{nullptr};

/// The media type the browser is told, from the suffixes the voice gallery
/// accepts. An unknown one is left to the browser to sniff.
QByteArray mediaTypeFor(const QString &path)
{
    const QString suffix{QFileInfo{path}.suffix().toLower()};
    if (suffix == QStringLiteral("wav")) {
        return QByteArrayLiteral("audio/wav");
    }
    if (suffix == QStringLiteral("mp3")) {
        return QByteArrayLiteral("audio/mpeg");
    }
    if (suffix == QStringLiteral("flac")) {
        return QByteArrayLiteral("audio/flac");
    }
    if (suffix == QStringLiteral("ogg")) {
        return QByteArrayLiteral("audio/ogg");
    }
    if (suffix == QStringLiteral("m4a")) {
        return QByteArrayLiteral("audio/mp4");
    }
    return {};
}
#endif

} // namespace

} // namespace Kerfur

#ifdef Q_OS_WASM

// The bytes are copied out of the wasm heap first: it is shared memory in this
// build, and a Blob refuses a view over a SharedArrayBuffer.
// clang-format off
EM_JS(void, kerfurPlayVoice, (const char *bytes, int size, const char *type, int token), {
    const previous = Module.kerfurVoice;
    if (previous) {
        Module.kerfurVoice = null;
        previous.audio.pause();
        URL.revokeObjectURL(previous.url);
    }
    const copy = new Uint8Array(size);
    copy.set(new Uint8Array(wasmMemory.buffer, bytes, size));
    const mediaType = UTF8ToString(type);
    const blob = mediaType ? new Blob([copy], {type: mediaType}) : new Blob([copy]);
    const url = URL.createObjectURL(blob);
    const audio = new Audio(url);
    const current = {audio: audio, url: url};
    Module.kerfurVoice = current;
    const finish = function(failed) {
        if (Module.kerfurVoice !== current) {
            return;
        }
        Module.kerfurVoice = null;
        URL.revokeObjectURL(url);
        _kerfurVoicePlaybackEnded(token, failed ? 1 : 0);
    };
    audio.onended = function() {
        finish(false);
    };
    audio.onerror = function() {
        console.warn("voice playback: the browser cannot play this recording");
        finish(true);
    };
    audio.play().catch(function(error) {
        console.warn("voice playback: " + error);
        finish(true);
    });
})

EM_JS(void, kerfurStopVoice, (), {
    const current = Module.kerfurVoice;
    if (!current) {
        return;
    }
    Module.kerfurVoice = null;
    current.audio.pause();
    URL.revokeObjectURL(current.url);
})
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void kerfurVoicePlaybackEnded(int token, int failed)
{
    if (Kerfur::playbackOwner != nullptr) {
        QMetaObject::invokeMethod(Kerfur::playbackOwner,
                                  "applyPlaybackEnded",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, token),
                                  Q_ARG(bool, failed != 0));
    }
}

#endif

namespace Kerfur {

#ifdef Q_OS_WASM

struct VoicePlayer::Backend
{};

#else

struct VoicePlayer::Backend
{
    QMediaPlayer player;
    QAudioOutput output;
};

#endif

VoicePlayer::VoicePlayer(QObject *parent) : QObject{parent}, m_backend{std::make_unique<Backend>()}
{
#ifndef Q_OS_WASM
    QMediaPlayer &player{m_backend->player};
    player.setAudioOutput(&m_backend->output);
    connect(&player,
            &QMediaPlayer::playbackStateChanged,
            this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState) {
                    applyPlaybackEnded(m_token, false);
                }
            });
    connect(&player,
            &QMediaPlayer::errorOccurred,
            this,
            [this](QMediaPlayer::Error error, const QString &message) {
                if (error != QMediaPlayer::NoError) {
                    qCWarning(playerLog) << "cannot play" << m_playing << message;
                    setProblem(message);
                    applyPlaybackEnded(m_token, true);
                }
            });
#endif
}

VoicePlayer::~VoicePlayer()
{
#ifdef Q_OS_WASM
    if (playbackOwner == this) {
        kerfurStopVoice();
        playbackOwner = nullptr;
    }
#endif
}

void VoicePlayer::toggle(const QUrl &recording)
{
    if (!m_playing.isEmpty() && m_playing == recording) {
        stop();
        return;
    }
    play(recording);
}

void VoicePlayer::stop()
{
    // An end the browser reports later for this recording is queued, and
    // would otherwise stop whichever recording is started next.
    ++m_token;
#ifdef Q_OS_WASM
    if (playbackOwner == this) {
        kerfurStopVoice();
    }
#else
    m_backend->player.stop();
#endif
    setPlaying(QUrl{});
}

void VoicePlayer::play(const QUrl &recording)
{
    stop();
    if (recording.isEmpty()) {
        return;
    }

#ifdef Q_OS_WASM
    const QString path{recording.isLocalFile() ? recording.toLocalFile() : recording.path()};
    QFile file{path};
    if (file.size() > maximumRecordingBytes) {
        setProblem(QStringLiteral("%1 is too large to play.").arg(QFileInfo{path}.fileName()));
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(playerLog) << "cannot read" << path << file.errorString();
        setProblem(QStringLiteral("Cannot read %1.").arg(QFileInfo{path}.fileName()));
        return;
    }
    const QByteArray content{file.readAll()};
    playbackOwner = this;
    setProblem(QString{});
    setPlaying(recording);
    kerfurPlayVoice(content.constData(),
                    static_cast<int>(content.size()),
                    mediaTypeFor(path).constData(),
                    m_token);
#else
    setProblem(QString{});
    setPlaying(recording);
    m_backend->player.setSource(recording);
    m_backend->player.play();
#endif
}

void VoicePlayer::applyPlaybackEnded(int token, bool failed)
{
    if (token != m_token) {
        return;
    }
    if (failed && m_problem.isEmpty()) {
        setProblem(QStringLiteral("Cannot play %1.").arg(m_playing.fileName()));
    }
    setPlaying(QUrl{});
}

void VoicePlayer::setPlaying(const QUrl &recording)
{
    if (m_playing == recording) {
        return;
    }
    m_playing = recording;
    emit playingChanged();
}

void VoicePlayer::setProblem(const QString &problem)
{
    if (m_problem == problem) {
        return;
    }
    m_problem = problem;
    emit problemChanged();
}

} // namespace Kerfur
