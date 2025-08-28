#include "WhisperEngine.hpp"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <cstring>
#include <algorithm>
#include <whisper.h>

WhisperEngine::WhisperEngine(QObject *parent)
: QObject{parent}
, m_params{std::make_unique<whisper_full_params>()}
{
    // Setup processing timer
    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(500); // Process every 500ms
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
        // Try common locations
        QStringList searchPaths = {
            "models/ggml-tiny.en.bin",
            "../models/ggml-tiny.en.bin",
            "../../models/ggml-tiny.en.bin",
            QDir::homePath() + "/.local/share/kerfur/models/ggml-tiny.en.bin"
        };

        bool found = false;
        for (const auto &path : searchPaths) {
            if (QFile::exists(path)) {
                qDebug() << "Found model at:" << path;
                return loadModel(path);
            }
        }

        if (!found) {
            m_lastError = "Could not find Whisper model. Please run 'make download-models'";
            qWarning() << m_lastError;
            return false;
        }
    }

    return loadModel(modelPath);
}

bool WhisperEngine::loadModel(const QString &modelPath)
{
    try {
        // Initialize whisper with the new API
        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;  // Try to use GPU if available

        m_context = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);

        if (!m_context) {
            m_lastError = QString{"Failed to load Whisper model from: %1"}.arg(modelPath);
            qWarning() << m_lastError;
            return false;
        }

        // Setup default parameters for English transcription
        *m_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        m_params->print_realtime = false;
        m_params->print_progress = false;
        m_params->print_timestamps = false;
        m_params->print_special = false;
        m_params->translate = m_translate;
        m_params->language = m_language.toUtf8().constData();
        m_params->n_threads = 4; // Use 4 threads for processing
        m_params->n_max_text_ctx = 16384;
        m_params->offset_ms = 0;
        m_params->duration_ms = 0;
        m_params->single_segment = false;
        m_params->max_tokens = 0;

        // These fields may not exist in newer versions, so we comment them out
        // m_params->speed_up = false;  // This field doesn't exist anymore
        m_params->suppress_blank = true;
        // m_params->suppress_non_speech_tokens = true;  // This field doesn't exist anymore

        // For real-time processing
        m_params->no_context = true;

        m_initialized = true;
        m_lastError.clear();

        // Start processing timer
        m_processTimer->start();

        qDebug() << "WhisperEngine initialized successfully with model:" << modelPath;
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

        // Need at least minimum samples
        if (m_audioBuffer.size() < MIN_SAMPLES_FOR_PROCESSING) {
            m_processing = false;
            return;
        }

        // Take a chunk for processing
        int samplesToTake = std::min(CHUNK_SIZE_SAMPLES, static_cast<int>(m_audioBuffer.size()));
        samplesToProcess.assign(m_audioBuffer.begin(), m_audioBuffer.begin() + samplesToTake);

        // Remove processed samples from buffer
        m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + samplesToTake);
    }

    // Process in a separate thread to avoid blocking
    std::thread([this, samplesToProcess]() {
        emit processingStarted();

        auto result = transcribe(samplesToProcess);

        if (!result.text.isEmpty()) {
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

    // Run whisper
    int ret = whisper_full(m_context, *m_params, samples.data(), samples.size());

    if (ret != 0) {
        qWarning() << "Whisper processing failed with code:" << ret;
        return result;
    }

    // Get the transcribed text
    const int n_segments = whisper_full_n_segments(m_context);

    QString fullText;
    float totalConfidence = 0.0f;
    int validSegments = 0;

    for (int i = 0; i < n_segments; ++i) {
        const char *text = whisper_full_get_segment_text(m_context, i);
        if (text && strlen(text) > 0) {
            fullText += QString::fromUtf8(text).trimmed() + " ";

            // Calculate average probability for confidence
            const int n_tokens = whisper_full_n_tokens(m_context, i);
            float segmentProb = 0.0f;
            for (int j = 0; j < n_tokens; ++j) {
                auto token_data = whisper_full_get_token_data(m_context, i, j);
                segmentProb += token_data.p;
            }
            if (n_tokens > 0) {
                totalConfidence += segmentProb / n_tokens;
                validSegments++;
            }
        }
    }

    result.text = fullText.trimmed();
    result.confidence = validSegments > 0 ? totalConfidence / validSegments : 0.0f;
    result.timestamp_ms = 0; // Could calculate from segments if needed

    return result;
}

void WhisperEngine::checkForKeywords(const QString &text)
{
    QString lowerText = text.toLower();

    for (const auto &keyword : m_keywords) {
        if (lowerText.contains(keyword.toLower())) {
            emit keywordDetected(keyword);
            qDebug() << "Keyword detected:" << keyword << "in text:" << text;
            break; // Only emit once per transcription
        }
    }
}

std::vector<float> WhisperEngine::convertToFloat(const QByteArray &audioData)
{
    const int16_t *samples = reinterpret_cast<const int16_t*>(audioData.constData());
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
}

void WhisperEngine::setLanguage(const QString &lang)
{
    m_language = lang;
    if (m_params) {
        m_params->language = m_language.toUtf8().constData();
    }
}

void WhisperEngine::enableTranslation(bool enable)
{
    m_translate = enable;
    if (m_params) {
        m_params->translate = enable;
    }
}
