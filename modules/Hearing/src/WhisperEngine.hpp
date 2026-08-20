// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

struct whisper_context;
struct whisper_full_params;

/// Transcribes complete utterances, one at a time, on a thread of its own.
///
/// It is given whole utterances rather than a stream. Whisper is not a
/// streaming recogniser: it decodes a window from the beginning every time it
/// is called, so feeding it overlapping windows on a timer costs one full
/// decode per window and returns the same sentence again and again. The
/// segmentation that turns the stream into utterances is VoiceActivitySegmenter
/// and it lives on the audio side, where the samples already are.
class WhisperEngine : public QObject
{
    Q_OBJECT

public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine() override;

    bool initialize(const QString &modelPath);
    bool isInitialized() const { return m_initialized; }
    QString getLastError() const { return m_lastError; }

    /// Queues one utterance of 16 kHz mono samples. Returns false when the
    /// queue was full and the oldest was dropped to make room, which is the
    /// honest thing to do with speech that is already stale.
    ///
    /// `sequence` is the caller's number for this utterance and is handed back
    /// with the transcript. Speaker identification runs on the same audio on a
    /// different thread and finishes in its own time, so without it there is
    /// nothing joining "what was said" to "who said it" but the hope that both
    /// answers arrive in the order they were asked for.
    bool submit(std::vector<float> samples, quint64 sequence);

    /// Drops everything queued. Used when the microphone stops, so that a
    /// backlog does not surface minutes later as a sentence from the last
    /// session.
    void reset();

    int pending() const;

    void setLanguage(const QString &language);
    void enableTranslation(bool enable);

signals:
    void transcriptionReady(const QString &text, float confidence, quint64 sequence);
    void keywordDetected(const QString &keyword);
    void processingStarted();
    void processingFinished();

private:
    bool loadModel(const QString &modelPath);
    void workerLoop();
    void transcribe(const std::vector<float> &samples, quint64 sequence);

    /// Routes whisper.cpp's and ggml's own printf logging into a Qt category.
    ///
    /// Both libraries write a model card to stderr on every load -- vocabulary
    /// size, tensor counts, buffer sizes -- and none of it is a fact about
    /// Kerfur. It is not turned off, because the day a model fails to load it
    /// is the only thing that says why; it is demoted to
    /// `kerfur.hearing.whisper.internal`, which the debug build shows and the
    /// release build does not. Called once, before the first context exists,
    /// because it is a process wide setting.
    static void routeLibraryLogs();

    whisper_context *m_context{nullptr};
    std::unique_ptr<whisper_full_params> m_params;

    bool m_initialized{false};
    QString m_lastError;

    /// Utterances waiting to be decoded. Bounded, because a machine that
    /// decodes slower than it is spoken to would otherwise queue for ever and
    /// answer questions from a minute ago.
    static constexpr size_t maximumPending{4};
    /// One queued utterance and the caller's number for it.
    struct Job
    {
        std::vector<float> samples;
        quint64 sequence{0};
    };
    std::deque<Job> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueSignal;
    std::thread m_worker;
    bool m_stopping{false};

    static constexpr int sampleRate{16000};

    /// Whisper pads anything shorter than a second to a second, and decodes the
    /// padding as words. Below this there is nothing worth asking it about.
    static constexpr size_t minimumSamples{sampleRate / 4};

    QString m_language{QStringLiteral("en")};

    // whisper_full_params::language is a borrowed const char*, so the bytes it
    // points at must outlive every call that reads them. Keeping the encoded
    // form here is what makes that true: assigning it from a temporary
    // QByteArray leaves the pointer dangling as soon as the statement ends.
    QByteArray m_languageUtf8{"en"};
    bool m_translate{false};
};
