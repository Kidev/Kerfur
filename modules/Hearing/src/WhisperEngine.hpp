#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QByteArray>
#include <QString>
#include <QTimer>

// Forward declarations
struct whisper_context;
struct whisper_full_params;

class WhisperEngine : public QObject
{
    Q_OBJECT

public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine();

    bool initialize(const QString &modelPath);
    bool isInitialized() const { return m_initialized; }

    struct TranscriptionResult
    {
        QString text;
        float confidence;
        int64_t timestamp_ms;
        std::vector<std::pair<QString, float>> tokens;
    };

    // Process audio data (expects 16kHz mono float32 samples)
    void processAudio(const std::vector<float> &samples);
    void reset();
    QString getLastError() const { return m_lastError; }

    // Configuration
    void setLanguage(const QString &lang);
    void enableTranslation(bool enable);

signals:
    void transcriptionReady(const QString &text, float confidence);
    void partialTranscription(const QString &text);
    void keywordDetected(const QString &keyword);
    void processingStarted();
    void processingFinished();

private slots:
    void processAudioBuffer();
    void checkForKeywords(const QString &text);

private:
    bool loadModel(const QString &modelPath);
    TranscriptionResult transcribe(const std::vector<float> &samples);

    // Convert int16 to float samples
    std::vector<float> convertToFloat(const QByteArray &audioData);

    whisper_context *m_context{nullptr};
    std::unique_ptr<whisper_full_params> m_params;

    bool m_initialized{false};
    QString m_lastError;
    QMutex m_mutex;

    // Audio buffer management
    std::vector<float> m_audioBuffer;
    QMutex m_bufferMutex;
    std::atomic<bool> m_processing{false};

    // Processing thread
    std::unique_ptr<std::thread> m_processingThread;
    std::atomic<bool> m_shouldStop{false};

    // Configuration
    static constexpr int SAMPLE_RATE{16000};
    static constexpr int CHUNK_SIZE_MS{3000}; // Process 3 second chunks
    static constexpr int CHUNK_SIZE_SAMPLES{SAMPLE_RATE * CHUNK_SIZE_MS / 1000};
    static constexpr int MIN_SAMPLES_FOR_PROCESSING{SAMPLE_RATE / 2}; // 0.5 seconds minimum

    QString m_language{"en"};

    // whisper_full_params::language is a borrowed const char*, so the bytes it
    // points at must outlive every call that reads them. Keeping the encoded
    // form here is what makes that true: assigning it from a temporary
    // QByteArray leaves the pointer dangling as soon as the statement ends.
    QByteArray m_languageUtf8{"en"};
    bool m_translate{false};

    // Keywords to detect
    QStringList m_keywords{"kerfur", "kerf", "fur", "kerfür", "care fur", "curve fur"};

    // Processing timer
    QTimer *m_processTimer{nullptr};
};
