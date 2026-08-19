#include "WhisperEngine.hpp"
#include <algorithm>
#include <cstring>
#include <whisper.h>
#include <QDebug>
#include <QDir>
#include <QFile>

WhisperEngine::WhisperEngine(QObject *parent)
    : QObject{parent}, m_params{std::make_unique<whisper_full_params>()}
{
    // Setup processing timer
    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(
        1000); // Process every 1 second (less frequent for better transcription)
    connect(m_processTimer, &QTimer::timeout, this, &WhisperEngine::processAudioBuffer);
}

WhisperEngine::~WhisperEngine()
{
    m_shouldStop = true;

    if (m_processingThread && m_processingThread->joinable()) {
        m_processingThread->join();
    }

    if (m_context) {
        whisper_free(m_context);
        m_context = nullptr;
    }
}

bool WhisperEngine::initialize(const QString &modelPath)
{
    QMutexLocker locker{&m_mutex};

    if (m_initialized) {
        qDebug() << "WhisperEngine already initialized";
        return true;
    }

    // Check if model file exists
    QFile modelFile{modelPath};
    if (!modelFile.exists()) {
        qDebug() << "WhisperEngine model not found at " << modelPath;
        m_lastError = QString("WhisperEngine model not found at %1").arg(modelPath);
        return false;
    }

    return loadModel(modelPath);
}

bool WhisperEngine::loadModel(const QString &modelPath)
{
    try {
        qDebug() << "Loading Whisper model from:" << modelPath;

        // Initialize whisper context params - be more conservative with GPU
        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false; // Disable GPU to avoid issues, use CPU only

        m_context = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);

        if (!m_context) {
            m_lastError = QString{"Failed to load Whisper model from: %1"}.arg(modelPath);
            qWarning() << m_lastError;
            return false;
        }

        // Setup default parameters for English transcription
        *m_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        // Configure parameters for real-time processing
        m_params->print_realtime = false;
        m_params->print_progress = false;
        m_params->print_timestamps = false;
        m_params->print_special = false;
        m_params->translate = m_translate;
        m_params->language = m_language.toUtf8().constData();
        m_params->n_threads =
            std::min(4,
                     static_cast<int>(
                         std::thread::hardware_concurrency())); // Use available threads but max 4
        m_params->n_max_text_ctx = 16384;
        m_params->offset_ms = 0;
        m_params->duration_ms = 0;
        m_params->single_segment = false; // Allow multiple segments
        m_params->max_tokens = 0;
        m_params->suppress_blank = true;

        // For better real-time performance
        m_params->no_context = false; // Keep context for better accuracy
        m_params->audio_ctx = 1500;   // Reduce audio context for real-time

        // Temperature settings for more stable output
        m_params->temperature = 0.0f; // Use greedy decoding for deterministic output
        m_params->temperature_inc = 0.0f;

        m_initialized = true;
        m_lastError.clear();

        // Start processing timer
        m_processTimer->start();

        qDebug() << "WhisperEngine initialized successfully with model:" << modelPath;
        qDebug() << "Using" << m_params->n_threads << "threads for processing";
        return true;

    } catch (const std::exception &e) {
        m_lastError = QString{"Exception during initialization: %1"}.arg(e.what());
        qWarning() << m_lastError;
        return false;
    }
}

void WhisperEngine::processAudio(const std::vector<float> &samples)
{
    if (!m_initialized || samples.empty())
        return;

    QMutexLocker locker{&m_bufferMutex};

    // Append to buffer
    m_audioBuffer.insert(m_audioBuffer.end(), samples.begin(), samples.end());

    // Keep buffer size reasonable (max 30 seconds)
    const int maxBufferSize = SAMPLE_RATE * 30;
    if (m_audioBuffer.size() > maxBufferSize) {
        // Remove older audio, keep most recent
        m_audioBuffer.erase(m_audioBuffer.begin(),
                            m_audioBuffer.begin() + (m_audioBuffer.size() - maxBufferSize));
    }
}

void WhisperEngine::processAudioBuffer()
{
    if (!m_initialized || m_processing.exchange(true))
        return;

    std::vector<float> samplesToProcess;

    {
        QMutexLocker locker{&m_bufferMutex};

        // Need at least minimum samples for meaningful transcription
        if (m_audioBuffer.size() < MIN_SAMPLES_FOR_PROCESSING) {
            m_processing = false;
            return;
        }

        // Take a larger chunk for better transcription accuracy
        int samplesToTake = std::min(CHUNK_SIZE_SAMPLES, static_cast<int>(m_audioBuffer.size()));

        // Always process from the end (most recent audio)
        auto startIt = m_audioBuffer.end() - samplesToTake;
        samplesToProcess.assign(startIt, m_audioBuffer.end());

        // Keep some overlap for context but remove older samples
        if (m_audioBuffer.size() > CHUNK_SIZE_SAMPLES) {
            int removeCount = m_audioBuffer.size() -
                              (CHUNK_SIZE_SAMPLES / 2); // Keep half for overlap
            m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + removeCount);
        }
    }

    // Process in a separate thread to avoid blocking
    std::thread([this, samplesToProcess]() {
        emit processingStarted();

        auto result = transcribe(samplesToProcess);

        if (!result.text.isEmpty() && result.text.length() > 3) { // Filter very short results
            emit transcriptionReady(result.text, result.confidence);
            checkForKeywords(result.text);
        }

        emit processingFinished();
        m_processing = false;
    }).detach();
}

WhisperEngine::TranscriptionResult WhisperEngine::transcribe(const std::vector<float> &samples)
{
    TranscriptionResult result;

    if (!m_context || samples.empty())
        return result;

    QMutexLocker locker{&m_mutex};

    try {
        qDebug() << "Transcribing" << samples.size() << "samples ("
                 << (samples.size() / static_cast<float>(SAMPLE_RATE)) << "seconds)";

        // Run whisper
        int ret = whisper_full(m_context, *m_params, samples.data(), samples.size());

        if (ret != 0) {
            qWarning() << "Whisper processing failed with code:" << ret;
            return result;
        }

        // Get the transcribed text
        const int n_segments = whisper_full_n_segments(m_context);
        qDebug() << "Got" << n_segments << "segments from whisper";

        QString fullText;
        float totalConfidence = 0.0f;
        int validSegments = 0;

        for (int i = 0; i < n_segments; ++i) {
            const char *text = whisper_full_get_segment_text(m_context, i);
            if (text && strlen(text) > 0) {
                QString segmentText = QString::fromUtf8(text).trimmed();

                // Filter out very short or nonsensical segments
                if (segmentText.length() >= 2) {
                    fullText += segmentText + " ";

                    // Calculate average probability for confidence
                    const int n_tokens = whisper_full_n_tokens(m_context, i);
                    float segmentProb = 0.0f;
                    int validTokens = 0;

                    for (int j = 0; j < n_tokens; ++j) {
                        auto token_data = whisper_full_get_token_data(m_context, i, j);
                        if (token_data.p > 0.0f) { // Only count valid probabilities
                            segmentProb += token_data.p;
                            validTokens++;
                        }
                    }

                    if (validTokens > 0) {
                        totalConfidence += segmentProb / validTokens;
                        validSegments++;
                    }

                    qDebug() << "Segment" << i << ":" << segmentText << "tokens:" << validTokens
                             << "prob:" << (validTokens > 0 ? segmentProb / validTokens : 0.0f);
                }
            }
        }

        result.text = fullText.trimmed();
        result.confidence = validSegments > 0 ? totalConfidence / validSegments : 0.0f;
        result.timestamp_ms = 0; // Could calculate from segments if needed

        qDebug() << "Final transcription result:" << result.text
                 << "confidence:" << result.confidence << "segments:" << validSegments;

    } catch (const std::exception &e) {
        qWarning() << "Exception during transcription:" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception during transcription";
    }

    return result;
}

void WhisperEngine::checkForKeywords(const QString &text)
{
    QString lowerText = text.toLower();

    // More comprehensive keyword detection
    for (const auto &keyword : m_keywords) {
        QString lowerKeyword = keyword.toLower();
        if (lowerText.contains(lowerKeyword)) {
            emit keywordDetected(keyword);
            qDebug() << "Keyword detected:" << keyword << "in text:" << text;
            return; // Only emit once per transcription
        }
    }

    // Also check for phonetic variations
    QStringList phoneticVariations = {"care for", "care furr", "care four"};

    for (const auto &variation : phoneticVariations) {
        if (lowerText.contains(variation)) {
            emit keywordDetected("kerfur"); // Always emit "kerfur" regardless of variation
            qDebug() << "Phonetic keyword detected:" << variation << "in text:" << text;
            return;
        }
    }
}

std::vector<float> WhisperEngine::convertToFloat(const QByteArray &audioData)
{
    const int16_t *samples = reinterpret_cast<const int16_t *>(audioData.constData());
    int sampleCount = audioData.size() / sizeof(int16_t);

    std::vector<float> floatSamples;
    floatSamples.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        floatSamples.push_back(samples[i] / 32768.0f);
    }

    return floatSamples;
}

void WhisperEngine::reset()
{
    QMutexLocker locker{&m_bufferMutex};
    m_audioBuffer.clear();
    qDebug() << "WhisperEngine audio buffer reset";
}

void WhisperEngine::setLanguage(const QString &lang)
{
    m_language = lang;
    if (m_params) {
        m_params->language = m_language.toUtf8().constData();
    }
    qDebug() << "WhisperEngine language set to:" << lang;
}

void WhisperEngine::enableTranslation(bool enable)
{
    m_translate = enable;
    if (m_params) {
        m_params->translate = enable;
    }
    qDebug() << "WhisperEngine translation:" << (enable ? "enabled" : "disabled");
}
