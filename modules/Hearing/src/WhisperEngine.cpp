// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WhisperEngine.hpp"

#include "transcriptfilter.h"

#include <whisper.h>

#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

Q_LOGGING_CATEGORY(whisperLog, "kerfur.hearing.whisper")

} // namespace

WhisperEngine::WhisperEngine(QObject *parent)
    : QObject{parent}, m_params{std::make_unique<whisper_full_params>()}
{}

WhisperEngine::~WhisperEngine()
{
    {
        const std::lock_guard<std::mutex> guard{m_queueMutex};
        m_stopping = true;
        m_queue.clear();
    }
    m_queueSignal.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }

    // After the join, and only after: the worker is the only thing that reads
    // the context, and freeing it underneath a decode in progress is the crash
    // that only happens on shutdown and only under load.
    if (m_context != nullptr) {
        whisper_free(m_context);
        m_context = nullptr;
    }
}

bool WhisperEngine::initialize(const QString &modelPath)
{
    if (m_initialized) {
        return true;
    }

    if (!QFileInfo::exists(modelPath)) {
        m_lastError = QStringLiteral("model not found at %1").arg(modelPath);
        return false;
    }

    return loadModel(modelPath);
}

bool WhisperEngine::loadModel(const QString &modelPath)
{
    whisper_context_params contextParams{whisper_context_default_params()};
    contextParams.use_gpu = false;

    m_context = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), contextParams);
    if (m_context == nullptr) {
        m_lastError = QStringLiteral("whisper could not read %1").arg(modelPath);
        qCWarning(whisperLog) << m_lastError;
        return false;
    }

    *m_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    m_params->print_realtime = false;
    m_params->print_progress = false;
    m_params->print_timestamps = false;
    m_params->print_special = false;
    m_params->translate = m_translate;
    m_params->language = m_languageUtf8.constData();
    m_params->n_threads = std::max(1,
                                   std::min(4,
                                            static_cast<int>(
                                                std::thread::hardware_concurrency())));
    m_params->suppress_blank = true;

    // Each utterance decoded on its own. Carrying the previous transcript
    // forward as a prompt is what made the model repeat itself: given the last
    // sentence as context and an overlapping window of the same audio, the
    // likeliest continuation is that sentence again. Utterances are
    // independent, so the context is not merely unnecessary here, it is wrong.
    m_params->no_context = true;
    m_params->single_segment = false;

    // Greedy with the default fallback ladder. A zero increment disables the
    // fallback entirely, and a decode that fails then returns nothing at all
    // rather than a second attempt at a higher temperature.
    m_params->temperature = 0.0F;

    m_initialized = true;
    m_lastError.clear();

    m_worker = std::thread{[this]() { workerLoop(); }};

    qCInfo(whisperLog) << "loaded" << modelPath << "on" << m_params->n_threads << "threads";
    return true;
}

bool WhisperEngine::submit(std::vector<float> samples)
{
    if (!m_initialized || samples.size() < minimumSamples) {
        return true;
    }

    bool dropped{false};
    {
        const std::lock_guard<std::mutex> guard{m_queueMutex};
        if (m_stopping) {
            return true;
        }
        while (m_queue.size() >= maximumPending) {
            m_queue.pop_front();
            dropped = true;
        }
        m_queue.push_back(std::move(samples));
    }
    m_queueSignal.notify_one();

    if (dropped) {
        qCWarning(whisperLog) << "transcription is behind; dropped the oldest utterance";
    }
    return !dropped;
}

void WhisperEngine::reset()
{
    const std::lock_guard<std::mutex> guard{m_queueMutex};
    m_queue.clear();
}

int WhisperEngine::pending() const
{
    const std::lock_guard<std::mutex> guard{m_queueMutex};
    return static_cast<int>(m_queue.size());
}

void WhisperEngine::workerLoop()
{
    for (;;) {
        std::vector<float> utterance;
        {
            std::unique_lock<std::mutex> lock{m_queueMutex};
            m_queueSignal.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
            if (m_stopping) {
                return;
            }
            utterance = std::move(m_queue.front());
            m_queue.pop_front();
        }

        emit processingStarted();
        transcribe(utterance);
        emit processingFinished();
    }
}

void WhisperEngine::transcribe(const std::vector<float> &samples)
{
    if (m_context == nullptr || samples.empty()) {
        return;
    }

    try {
        const int status{
            whisper_full(m_context, *m_params, samples.data(), static_cast<int>(samples.size()))};
        if (status != 0) {
            qCWarning(whisperLog) << "decode failed with" << status;
            return;
        }

        QString text;
        double totalProbability{0.0};
        int countedTokens{0};

        const int segments{whisper_full_n_segments(m_context)};
        for (int segment{0}; segment < segments; ++segment) {
            const char *raw{whisper_full_get_segment_text(m_context, segment)};
            if (raw == nullptr) {
                continue;
            }
            const QString cleaned{Kerfur::cleanTranscript(QString::fromUtf8(raw))};
            if (cleaned.isEmpty()) {
                continue;
            }
            if (!text.isEmpty()) {
                text.append(QLatin1Char{' '});
            }
            text.append(cleaned);

            const int tokens{whisper_full_n_tokens(m_context, segment)};
            for (int token{0}; token < tokens; ++token) {
                const whisper_token_data data{
                    whisper_full_get_token_data(m_context, segment, token)};
                if (data.p > 0.0F) {
                    totalProbability += data.p;
                    ++countedTokens;
                }
            }
        }

        if (text.isEmpty()) {
            qCDebug(whisperLog) << "utterance of" << samples.size() / sampleRate
                                << "s carried no speech";
            return;
        }

        const float confidence{countedTokens > 0
                                   ? static_cast<float>(totalProbability / countedTokens)
                                   : 0.0F};
        qCInfo(whisperLog) << "heard" << text << "at" << confidence;

        emit transcriptionReady(text, confidence);
        if (Kerfur::containsWakeWord(text)) {
            emit keywordDetected(QStringLiteral("kerfur"));
        }
    } catch (const std::exception &error) {
        qCWarning(whisperLog) << "decode threw" << error.what();
    }
}

void WhisperEngine::setLanguage(const QString &language)
{
    m_language = language;
    m_languageUtf8 = m_language.toUtf8();
    if (m_params) {
        m_params->language = m_languageUtf8.constData();
    }
}

void WhisperEngine::enableTranslation(bool enable)
{
    m_translate = enable;
    if (m_params) {
        m_params->translate = enable;
    }
}
